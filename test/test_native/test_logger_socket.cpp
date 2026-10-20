#include <gtest/gtest.h>
#include "logger/LoggerSocketSink.h"
#include "logger/LogRecordBuffer.h"
#include "logger/LogRecordWriter.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <vector>
#include <array>
#include <atomic>
#include <string>

using namespace ib::logger;
using namespace std::chrono_literals;
using Level = LoggerInterface::LogLevel;

namespace {
template <class Predicate>
bool until(Predicate predicate, std::chrono::milliseconds timeout = 2000ms) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (predicate())
			return true;
		std::this_thread::sleep_for(1ms);
	}
	return predicate();
}
struct Socket {
	int fd = -1;
	explicit Socket(int value = -1) : fd(value) {}
	~Socket() {
		if (fd >= 0)
			::close(fd);
	}
};
struct Listener : Socket {
	uint16_t port;
	explicit Listener(int receiveBuffer = 0) : Socket(::socket(AF_INET, SOCK_STREAM, 0)) {
		if (receiveBuffer)
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof(receiveBuffer));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(fd, 8))
			std::abort();
		socklen_t size = sizeof(address);
		getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size);
		port = ntohs(address.sin_port);
		fcntl(fd, F_SETFL, O_NONBLOCK);
	}
	int accept() { return ::accept(fd, nullptr, nullptr); }
};
std::string receive(int fd, size_t size) {
	std::string result;
	until([&] {
		char buffer[4096];
		const auto count = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
		if (count > 0)
			result.append(buffer, count);
		return result.size() >= size;
	});
	return result;
}
} // namespace

TEST(LogRecordBuffer, CopiesWrapsAndPreservesFragments) {
	detail::LogRecordBuffer queue(15);
	char source[] = "abc";
	ASSERT_TRUE(queue.push(source, 3));
	source[0] = 'x';
	ASSERT_TRUE(queue.push("defg", 4));
	char result[16]{};
	ASSERT_EQ(queue.pop(result), 3u);
	EXPECT_EQ(std::string(result, 3), "abc");
	ASSERT_TRUE(queue.push("hijkl", 5));
	EXPECT_FALSE(queue.push("z", 1));
	ASSERT_EQ(queue.pop(result), 4u);
	EXPECT_EQ(std::string(result, 4), "defg");
	ASSERT_EQ(queue.pop(result), 5u);
	EXPECT_EQ(std::string(result, 5), "hijkl");
	EXPECT_EQ(queue.used(), 0u);
	EXPECT_EQ(queue.highWater(), 13u);
}

TEST(LogRecordWriter, PartialWritesDoNotDuplicateBytes) {
	std::string output;
	int attempts = 0;
	auto result = detail::writeRecord(
		"abcdef", 6,
		[&](const char *data, size_t size) {
			if (++attempts == 2)
				return -1;
			auto count = std::min<size_t>(2, size);
			output.append(data, count);
			return static_cast<int>(count);
		},
		[] { return true; }, [] { return false; }, [] { return false; });
	EXPECT_EQ(output, "abcdef");
	EXPECT_EQ(result.sent, 6u);
	EXPECT_EQ(result.partialWrites, 2u);
	EXPECT_FALSE(result.error);
}

TEST(LogRecordWriter, ErrorAbandonsSuffixAndNextConnectionStartsFresh) {
	std::string output;
	auto first = detail::writeRecord(
		"abcdef", 6,
		[&](const char *data, size_t) {
			if (!output.empty())
				return -2;
			output.append(data, 2);
			return 2;
		},
		[] { return true; }, [] { return false; }, [] { return false; });
	EXPECT_EQ(first.sent, 2u);
	EXPECT_TRUE(first.error);
	auto second = detail::writeRecord(
		"NEW", 3,
		[&](const char *data, size_t size) {
			output.append(data, size);
			return static_cast<int>(size);
		},
		[] { return true; }, [] { return false; }, [] { return false; });
	EXPECT_EQ(second.sent, 3u);
	EXPECT_EQ(output, "abNEW");
}

TEST(LogRecordWriter, DeadlineIsNotRenewedByProgress) {
	int attempts = 0;
	auto result = detail::writeRecord(
		"abcdef", 6,
		[&](const char *, size_t) {
			++attempts;
			return 1;
		},
		[] { return true; }, [] { return false; }, [&] { return attempts >= 3; });
	EXPECT_EQ(result.sent, 3u);
	EXPECT_TRUE(result.timedOut);
}

TEST(LoggerSocketSink, BoundsDropsAndResourceFailures) {
	LoggerSocketSink sink(Level::INFO, "", 0, 100, 12, 5);
	sink.writeLog("abcde", 5);
	sink.writeLog("xyz", 3);
	sink.writeLog("z", 1);
	sink.writeLog("123456", 6);
	auto stats = sink.getStatistics();
	EXPECT_TRUE(stats.enabled);
	EXPECT_EQ(stats.acceptedRecords, 2u);
	EXPECT_EQ(stats.acceptedBytes, 8u);
	EXPECT_EQ(stats.bufferFull, 1u);
	EXPECT_EQ(stats.oversized, 1u);
	EXPECT_EQ(stats.droppedRecords, 2u);
	EXPECT_EQ(stats.droppedBytes, 7u);
	EXPECT_EQ(stats.queuedBytes, 12u);
	sink.setHost("", 1);
	stats = sink.getStatistics();
	EXPECT_EQ(stats.lostRecords, 2u);
	EXPECT_EQ(stats.lostBytes, 8u);
	EXPECT_EQ(stats.queuedBytes, 0u);
	LoggerSocketSink disabled(Level::INFO, "localhost", 1, 100, 0);
	disabled.writeLog("z", 1);
	EXPECT_FALSE(disabled.getStatistics().enabled);
	EXPECT_EQ(disabled.getStatistics().noResources, 2u);
}

TEST(LoggerSocketSink, TcpReceivesOwnCopyAndOrderedFragments) {
	Listener listener;
	LoggerSocketSink sink(Level::TRACE, "127.0.0.1", listener.port);
	Socket peer;
	ASSERT_TRUE(until([&] {
		peer.fd = listener.accept();
		return peer.fd >= 0;
	}));
	char data[] = "abc";
	sink.writeLog(data, 3);
	data[0] = 'X';
	sink.writeLog("def\n", 4);
	EXPECT_EQ(receive(peer.fd, 7), "abcdef\n");
	sink.setLevel(Level::WARN);
	EXPECT_EQ(sink.getLevel(), Level::WARN);
}

TEST(LoggerSocketSink, ParallelProducersAreBoundedAndAccounted) {
	LoggerSocketSink sink(Level::INFO, "", 0);
	std::vector<std::thread> threads;
	for (int producer = 0; producer < 8; ++producer)
		threads.emplace_back([&] {
			for (int i = 0; i < 10000; ++i)
				sink.writeLog("record", 6);
		});
	for (auto &thread : threads)
		thread.join();
	const auto stats = sink.getStatistics();
	EXPECT_EQ(stats.acceptedRecords + stats.droppedRecords, 80000u);
	EXPECT_EQ(stats.acceptedBytes + stats.droppedBytes, 480000u);
	EXPECT_EQ(stats.bufferFull + stats.lockBusy, stats.droppedRecords);
	EXPECT_LE(stats.peakQueuedBytes, 2048u);
	EXPECT_EQ(stats.queuedBytes, stats.acceptedRecords * 8);
}

TEST(LoggerSocketSink, ConcurrentProducersPreservePerProducerOrderAndWholeRecords) {
	Listener listener;
	LoggerSocketSink sink(Level::INFO, "127.0.0.1", listener.port, 1000, 65536, 32);
	Socket peer;
	ASSERT_TRUE(until([&] {
		peer.fd = listener.accept();
		return peer.fd >= 0;
	}));
	std::vector<std::thread> threads;
	for (int producer = 0; producer < 4; ++producer)
		threads.emplace_back([&, producer] {
			for (int i = 0; i < 100; ++i) {
				char record[32];
				snprintf(record, sizeof(record), "%d:%05d\n", producer, i);
				sink.writeLog(record, 8);
			}
		});
	for (auto &thread : threads)
		thread.join();
	const auto stats = sink.getStatistics();
	const auto output = receive(peer.fd, stats.acceptedBytes);
	ASSERT_EQ(output.size(), stats.acceptedBytes);
	std::array<int, 4> previous{-1, -1, -1, -1};
	for (size_t offset = 0; offset < output.size(); offset += 8) {
		int producer, sequence;
		ASSERT_EQ(sscanf(output.substr(offset, 8).c_str(), "%d:%d", &producer, &sequence), 2);
		ASSERT_GE(producer, 0);
		ASSERT_LT(producer, 4);
		EXPECT_GT(sequence, previous[producer]);
		previous[producer] = sequence;
		EXPECT_EQ(output[offset + 7], '\n');
	}
}

TEST(LoggerSocketSink, SlowReceiverDoesNotStopProducersAndShutdown) {
	Listener listener(1024);
	auto sink = std::make_unique<LoggerSocketSink>(Level::INFO, "127.0.0.1", listener.port, 50);
	Socket peer;
	ASSERT_TRUE(until([&] {
		peer.fd = listener.accept();
		return peer.fd >= 0;
	}));
	const std::string record(512, 'x');
	size_t calls = 0;
	const auto deadline = std::chrono::steady_clock::now() + 1500ms;
	while (std::chrono::steady_clock::now() < deadline) {
		sink->writeLog(record.data(), record.size());
		++calls;
		if (calls % 1000 == 0 && sink->getStatistics().writeTimeouts)
			break;
	}
	const auto stats = sink->getStatistics();
	EXPECT_GT(calls, 1000u);
	EXPECT_GT(stats.droppedRecords, 0u);
	EXPECT_GT(stats.writeTimeouts, 0u);
	EXPECT_GT(stats.lostRecords, 0u);
	EXPECT_LE(stats.peakQueuedBytes, 2048u);
	const auto start = std::chrono::steady_clock::now();
	sink.reset();
	EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
}

TEST(LoggerSocketSink, HostChangeDiscardsOldBacklogAndReconnects) {
	Listener listener;
	LoggerSocketSink sink(Level::INFO, "", 0, 200);
	sink.writeLog("old", 3);
	sink.setHost("localhost", listener.port);
	Socket peer;
	ASSERT_TRUE(until([&] {
		peer.fd = listener.accept();
		return peer.fd >= 0;
	}));
	sink.writeLog("new", 3);
	EXPECT_EQ(receive(peer.fd, 3), "new");
	EXPECT_EQ(sink.getStatistics().lostRecords, 1u);
	Listener second;
	sink.setHost("127.0.0.1", second.port);
	Socket next;
	ASSERT_TRUE(until([&] {
		next.fd = second.accept();
		return next.fd >= 0;
	}));
	sink.writeLog("next", 4);
	EXPECT_EQ(receive(next.fd, 4), "next");
	EXPECT_EQ(sink.getStatistics().reconnects, 1u);
}

TEST(LoggerSocketSink, ShutdownDuringConnectDnsAndQueuedWorkRepeatedly) {
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 20; ++i) {
		LoggerSocketSink sink(Level::INFO, i % 2 ? "192.0.2.1" : "async-logger-test.invalid", 9, 10000);
		sink.writeLog("queued", 6);
		std::this_thread::sleep_for(2ms);
	}
	EXPECT_LT(std::chrono::steady_clock::now() - start, 2500ms);
}

TEST(LoggerSocketSink, ResetDropsBacklogAndRecoversWithoutRecreation) {
	Listener listener;
	LoggerSocketSink sink(Level::INFO, "127.0.0.1", listener.port, 100);
	int peer = -1;
	ASSERT_TRUE(until([&] {
		peer = listener.accept();
		return peer >= 0;
	}));
	linger reset{1, 0};
	setsockopt(peer, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
	::close(peer);
	std::this_thread::sleep_for(5ms); // let the local TCP stack process the RST
	ASSERT_TRUE(until([&] {
		if (sink.getStatistics().acceptedRecords == 0)
			sink.writeLog("old", 3);
		return sink.getStatistics().acceptedRecords != 0;
	}));
	ASSERT_TRUE(until([&] { return sink.getStatistics().writeErrors != 0; }));
	EXPECT_GT(sink.getStatistics().lostRecords, 0u);
	Socket next;
	ASSERT_TRUE(until(
		[&] {
			next.fd = listener.accept();
			return next.fd >= 0;
		},
		12000ms));
	ASSERT_TRUE(until([&] { return sink.getStatistics().connected; }));
	sink.writeLog("recovered", 9);
	EXPECT_EQ(receive(next.fd, 9), "recovered");
	EXPECT_EQ(sink.getStatistics().reconnects, 1u);
}

TEST(LoggerSocketSink, HostChangeInterruptsActiveBackpressure) {
	Listener blocked(1024), replacement;
	LoggerSocketSink sink(Level::INFO, "127.0.0.1", blocked.port, 10000);
	Socket peer;
	ASSERT_TRUE(until([&] {
		peer.fd = blocked.accept();
		return peer.fd >= 0;
	}));
	std::string data(512, 'x');
	const auto deadline = std::chrono::steady_clock::now() + 1000ms;
	while (std::chrono::steady_clock::now() < deadline)
		sink.writeLog(data.data(), data.size());
	const auto start = std::chrono::steady_clock::now();
	sink.setHost("127.0.0.1", replacement.port);
	Socket next;
	ASSERT_TRUE(until([&] {
		next.fd = replacement.accept();
		return next.fd >= 0;
	}));
	EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
	sink.writeLog("new-host", 8);
	EXPECT_EQ(receive(next.fd, 8), "new-host");
}

TEST(LoggerSocketSink, ClosedPortCountsFailuresAndThrottlesRetries) {
	Listener listener;
	const auto port = listener.port;
	::close(listener.fd);
	listener.fd = -1;
	LoggerSocketSink sink(Level::INFO, "", 0, 50);
	sink.setHost("127.0.0.1", port);
	ASSERT_TRUE(until([&] { return sink.getStatistics().connectionFailures != 0; }));
	std::this_thread::sleep_for(100ms);
	EXPECT_EQ(sink.getStatistics().connectionFailures, 1u);
	EXPECT_FALSE(sink.getStatistics().connected);
}

namespace {
struct TestLogAllocator {
	bool psramAvailable = false, internalAvailable = true;
	std::vector<std::pair<size_t, LogBufferMemoryType>> requests;
	size_t allocations = 0, frees = 0;
	detail::LogMemoryAllocator allocator() {
		return {this,
		        [](void *context, size_t size, LogBufferMemoryType type) -> void * {
					auto &self = *static_cast<TestLogAllocator *>(context);
					self.requests.emplace_back(size, type);
					if ((type == LogBufferMemoryType::Psram && !self.psramAvailable) || (type == LogBufferMemoryType::InternalRam && !self.internalAvailable))
						return nullptr;
					void *result = std::malloc(size);
					if (result)
						++self.allocations;
					return result;
				},
		        [](void *context, void *memory) {
					++static_cast<TestLogAllocator *>(context)->frees;
					std::free(memory);
				}};
	}
};
} // namespace

TEST(LogRecordMemory, DefaultNeverRequestsPsram) {
	TestLogAllocator allocator;
	allocator.psramAvailable = true;
	{
		detail::LogRecordBuffer queue(2048, LogBufferMemoryPreference::InternalRam, 65536, allocator.allocator());
		EXPECT_EQ(queue.memoryType(), LogBufferMemoryType::InternalRam);
		EXPECT_EQ(queue.capacity(), 2048u);
		EXPECT_EQ(queue.psramFallbacks(), 0u);
		ASSERT_EQ(allocator.requests.size(), 1u);
		EXPECT_EQ(allocator.requests[0], std::make_pair(size_t(2048), LogBufferMemoryType::InternalRam));
	}
	EXPECT_EQ(allocator.allocations, 1u);
	EXPECT_EQ(allocator.frees, 1u);
}

TEST(LogRecordMemory, ExternalAllocationWrapsAt64KiBAndDoesNotAllocatePerRecord) {
	TestLogAllocator allocator;
	allocator.psramAvailable = true;
	{
		detail::LogRecordBuffer queue(2048, LogBufferMemoryPreference::PreferPsram, 65536, allocator.allocator());
		EXPECT_EQ(queue.memoryType(), LogBufferMemoryType::Psram);
		EXPECT_EQ(queue.capacity(), 65536u);
		EXPECT_EQ(queue.psramAllocationFailures(), 0u);
		EXPECT_EQ(queue.psramFallbacks(), 0u);
		char received[510];
		for (int i = 0; i < 128; ++i) {
			std::string data(510, static_cast<char>(i));
			ASSERT_TRUE(queue.push(data.data(), data.size()));
		}
		EXPECT_FALSE(queue.push("full", 4));
		for (int i = 0; i < 64; ++i) {
			ASSERT_EQ(queue.pop(received), 510u);
			EXPECT_EQ(std::string(received, 510), std::string(510, static_cast<char>(i)));
		}
		for (int i = 128; i < 192; ++i) {
			std::string data(510, static_cast<char>(i));
			ASSERT_TRUE(queue.push(data.data(), data.size()));
		}
		for (int i = 64; i < 192; ++i) {
			ASSERT_EQ(queue.pop(received), 510u);
			EXPECT_EQ(std::string(received, 510), std::string(510, static_cast<char>(i)));
		}
		EXPECT_EQ(queue.used(), 0u);
		EXPECT_EQ(queue.highWater(), 65536u);
		ASSERT_EQ(allocator.requests.size(), 1u);
		EXPECT_EQ(allocator.requests[0], std::make_pair(size_t(65536), LogBufferMemoryType::Psram));
	}
	EXPECT_EQ(allocator.allocations, 1u);
	EXPECT_EQ(allocator.frees, 1u);
}

TEST(LogRecordMemory, UnavailablePsramFallsBackOnlyToConfiguredSmallInternalBuffer) {
	// No support, uninitialized PSRAM, and fragmentation all produce the same
	// null allocation result. Policy must never retry 64 KiB in internal RAM.
	TestLogAllocator allocator;
	{
		detail::LogRecordBuffer queue(1024, LogBufferMemoryPreference::PreferPsram, 65536, allocator.allocator());
		EXPECT_EQ(queue.memoryType(), LogBufferMemoryType::InternalRam);
		EXPECT_EQ(queue.capacity(), 1024u);
		EXPECT_EQ(queue.psramAllocationFailures(), 1u);
		EXPECT_EQ(queue.internalAllocationFailures(), 0u);
		EXPECT_EQ(queue.psramFallbacks(), 1u);
		ASSERT_EQ(allocator.requests.size(), 2u);
		EXPECT_EQ(allocator.requests[0], std::make_pair(size_t(65536), LogBufferMemoryType::Psram));
		EXPECT_EQ(allocator.requests[1], std::make_pair(size_t(1024), LogBufferMemoryType::InternalRam));
		ASSERT_TRUE(queue.push("fallback", 8));
		char data[8];
		EXPECT_EQ(queue.pop(data), 8u);
		EXPECT_EQ(std::string(data, 8), "fallback");
	}
	EXPECT_EQ(allocator.allocations, 1u);
	EXPECT_EQ(allocator.frees, 1u);
}

TEST(LogRecordMemory, BothAllocationsFailWithoutOwningMemory) {
	TestLogAllocator allocator;
	allocator.internalAvailable = false;
	{
		detail::LogRecordBuffer queue(2048, LogBufferMemoryPreference::PreferPsram, 65536, allocator.allocator());
		EXPECT_EQ(queue.capacity(), 0u);
		EXPECT_EQ(queue.memoryType(), LogBufferMemoryType::None);
		EXPECT_EQ(queue.psramAllocationFailures(), 1u);
		EXPECT_EQ(queue.internalAllocationFailures(), 1u);
		EXPECT_EQ(queue.psramFallbacks(), 1u);
		EXPECT_FALSE(queue.push("x", 1));
	}
	EXPECT_EQ(allocator.allocations, 0u);
	EXPECT_EQ(allocator.frees, 0u);
}

TEST(LogRecordMemory, ConfiguredExternalSizeIsIndependentOfInternalFallback) {
	TestLogAllocator allocator;
	allocator.psramAvailable = true;
	detail::LogRecordBuffer queue(64, LogBufferMemoryPreference::PreferPsram, 1024, allocator.allocator());
	EXPECT_EQ(queue.capacity(), 1024u);
	ASSERT_EQ(allocator.requests.size(), 1u);
	EXPECT_EQ(allocator.requests[0], std::make_pair(size_t(1024), LogBufferMemoryType::Psram));
}

TEST(LoggerSocketSink, ReportsActualDefaultAndFallbackMemory) {
	LoggerSocketSink defaults(Level::INFO, "", 0, LoggerSocketSink::MemoryPreference::PreferPsram);
	EXPECT_EQ(defaults.getStatistics().bufferCapacity, 2048u);
	EXPECT_EQ(defaults.getStatistics().psramFallbacks, 1u);
	LoggerSocketSink internal(Level::INFO, "", 0);
	auto stats = internal.getStatistics();
	EXPECT_EQ(stats.memoryType, LoggerSocketSink::MemoryType::InternalRam);
	EXPECT_EQ(stats.bufferCapacity, 2048u);
	EXPECT_EQ(stats.psramAllocationFailures, 0u);
	EXPECT_EQ(stats.psramFallbacks, 0u);
	LoggerSocketSink external(Level::INFO, "", 0, LoggerSocketSink::MemoryPreference::PreferPsram, 1000, 128, 65536, 64);
	external.writeLog("works", 5);
	stats = external.getStatistics();
	EXPECT_TRUE(stats.enabled);
	EXPECT_EQ(stats.memoryType, LoggerSocketSink::MemoryType::InternalRam);
	EXPECT_EQ(stats.bufferCapacity, 128u);
	EXPECT_EQ(stats.psramAllocationFailures, 1u);
	EXPECT_EQ(stats.psramFallbacks, 1u);
	EXPECT_EQ(stats.acceptedRecords, 1u);
	EXPECT_EQ(stats.queuedBytes, 7u);
	LoggerSocketSink failed(Level::INFO, "", 0, LoggerSocketSink::MemoryPreference::PreferPsram, 1000, 0, 65536, 64);
	stats = failed.getStatistics();
	EXPECT_FALSE(stats.enabled);
	EXPECT_EQ(stats.memoryType, LoggerSocketSink::MemoryType::None);
	EXPECT_EQ(stats.bufferCapacity, 0u);
	EXPECT_EQ(stats.psramAllocationFailures, 1u);
	EXPECT_EQ(stats.internalAllocationFailures, 1u);
	EXPECT_EQ(stats.psramFallbacks, 1u);
	EXPECT_GT(stats.noResources, 0u);
}
