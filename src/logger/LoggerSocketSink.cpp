#include "LoggerSocketSink.h"
#include "LogRecordBuffer.h"
#include "LogRecordWriter.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#include <condition_variable>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#endif

namespace ib::logger {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto pollInterval = std::chrono::milliseconds(20);
constexpr auto reconnectDelay = std::chrono::seconds(10);
#ifdef ARDUINO_ARCH_ESP32
constexpr uint32_t workerStackBytes = 3072;
#endif

// A pending resolver owns its state independently of the sink. On ESP32 the
// lwIP thread owns one reference through completion, even after sink shutdown.
// At most one lookup per sink is outstanding; a timeout never starts another
// lookup until the earlier callback completes.
struct DnsRequest {
	std::atomic<unsigned> references{2};
	std::atomic<bool> done{false};
	bool success = false;
	in_addr address{};
	char host[254]{};
	void release() {
		if (references.fetch_sub(1) == 1)
			detail::destroyInternal(this);
	}
#ifdef ARDUINO_ARCH_ESP32
	static void complete(const char *, const ip_addr_t *address, void *argument) {
		auto *request = static_cast<DnsRequest *>(argument);
		request->success = address && IP_IS_V4(address);
		if (request->success)
			request->address.s_addr = ip4_addr_get_u32(ip_2_ip4(address));
		request->done.store(true, std::memory_order_release);
		request->release();
	}
	static void start(void *argument) {
		auto *request = static_cast<DnsRequest *>(argument);
		ip_addr_t address;
		const err_t result = dns_gethostbyname_addrtype(request->host, &address, complete, request, LWIP_DNS_ADDRTYPE_IPV4);
		if (result != ERR_INPROGRESS)
			complete(nullptr, result == ERR_OK ? &address : nullptr, request);
	}
#endif
};

DnsRequest *resolve(const char *host) {
	auto *request = detail::makeInternal<DnsRequest>();
	if (!request)
		return nullptr;
	std::memcpy(request->host, host, std::strlen(host) + 1);
#ifdef ARDUINO_ARCH_ESP32
	if (tcpip_try_callback(DnsRequest::start, request) != ERR_OK) {
		request->release();
		request->release();
		return nullptr;
	}
#else
	try {
		std::thread([request] {
			addrinfo hints{}, *result = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			request->success = getaddrinfo(request->host, nullptr, &hints, &result) == 0;
			if (request->success)
				request->address = reinterpret_cast<sockaddr_in *>(result->ai_addr)->sin_addr;
			if (result)
				freeaddrinfo(result);
			request->done.store(true, std::memory_order_release);
			request->release();
		}).detach();
	} catch (...) {
		request->release();
		request->release();
		return nullptr;
	}
#endif
	return request;
}

bool wouldBlock() {
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
}
} // namespace

struct LoggerSocketSink::State {
	detail::LogRecordBuffer queue;
	std::unique_ptr<char, detail::LogMemoryDeleter> record;
	const size_t maxLength;
	const std::chrono::milliseconds timeout;
	mutable std::mutex mutex;
	char host[254]{};
	uint16_t port = 0;
	std::atomic<uint32_t> generation{0};
	std::atomic<bool> stop{false}, enabled{false}, connected{false};
#define COUNTERS(X)   X(acceptedRecords) X(acceptedBytes) X(droppedRecords) X(droppedBytes) X(bufferFull) X(oversized) X(lockBusy) X(noResources) X(partialWrites) X(writeErrors) X(writeTimeouts) X(connectionFailures) X(reconnects) X(lostRecords) X(lostBytes) X(minimumFreeStack)
#define DECLARE(name) std::atomic<uint32_t> name{0};
	COUNTERS(DECLARE)
#undef DECLARE
#ifdef ARDUINO_ARCH_ESP32
	// Orders the destructor notification before the worker can delete its task.
	std::mutex shutdownMutex;
	TaskHandle_t task = nullptr;
	SemaphoreHandle_t finished = nullptr;
#else
	std::thread task;
	std::mutex wakeMutex;
	std::condition_variable wakeCondition;
	std::atomic<bool> notified{false};
#endif

	State(size_t capacity, size_t maximum, int32_t timeoutMs, MemoryPreference preference, size_t psramCapacity) : queue(capacity, preference, psramCapacity), record(maximum && maximum <= UINT16_MAX ? static_cast<char *>(detail::allocateLogMemory(maximum, MemoryType::InternalRam)) : nullptr), maxLength(maximum), timeout(std::max<int32_t>(1, std::min<int32_t>(10000, timeoutMs))) {}

	void wake() {
#ifdef ARDUINO_ARCH_ESP32
		if (task)
			xTaskNotifyGive(task);
#else
		notified.store(true);
		wakeCondition.notify_one();
#endif
	}
	void wait(std::chrono::milliseconds duration) {
#ifdef ARDUINO_ARCH_ESP32
		ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(duration.count())));
#else
		std::unique_lock<std::mutex> lock(wakeMutex);
		wakeCondition.wait_for(lock, duration, [this] { return notified.exchange(false) || stop.load(); });
#endif
	}
	bool interrupted(uint32_t version) const { return stop.load() || generation.load() != version; }
	void loseQueue() { // mutex held
		lostRecords.fetch_add(queue.records());
		lostBytes.fetch_add(queue.payloadBytes());
		queue.clear();
	}
	void discard(uint32_t version) {
		std::lock_guard<std::mutex> lock(mutex);
		if (generation.load() == version)
			loseQueue();
	}
	void publish(const std::string &name, uint16_t number) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			loseQueue();
			host[0] = '\0';
			if (name.size() <= 253 && name.find('\0') == std::string::npos)
				std::memcpy(host, name.c_str(), name.size() + 1);
			port = number;
			generation.fetch_add(1);
		}
		wake();
	}
	// Only the worker touches the socket. select is bounded to 20 ms, so config
	// changes/shutdown do not wait for DNS, connect or record deadlines.
	// lwIP close under memory pressure additionally waits for its TCP poll.
	bool writable(int socket, uint32_t version, Clock::time_point deadline) {
		while (!interrupted(version) && Clock::now() < deadline) {
			fd_set writes, errors;
			FD_ZERO(&writes);
			FD_ZERO(&errors);
			FD_SET(socket, &writes);
			FD_SET(socket, &errors);
			timeval interval{0, 20000};
			const int result = select(socket + 1, nullptr, &writes, &errors, &interval);
			if (result > 0)
				return !FD_ISSET(socket, &errors);
			if (result < 0 && errno != EINTR)
				return false;
		}
		return false;
	}
	bool addressFor(const char *name, in_addr &address, uint32_t version, DnsRequest *&dns) {
		if (inet_pton(AF_INET, name, &address) == 1)
			return true;
		if (dns && dns->done.load(std::memory_order_acquire) && std::strcmp(dns->host, name) != 0) {
			dns->release();
			dns = nullptr;
		}
		if (!dns) {
			dns = resolve(name);
			if (!dns) {
				++noResources;
				return false;
			}
		}
		const auto deadline = Clock::now() + timeout;
		while (!interrupted(version) && Clock::now() < deadline) {
			if (dns->done.load(std::memory_order_acquire)) {
				const bool success = dns->success && std::strcmp(dns->host, name) == 0;
				if (success)
					address = dns->address;
				dns->release();
				dns = nullptr;
				return success;
			}
			wait(pollInterval);
		}
		return false;
	}
	int open(const char *name, uint16_t number, uint32_t version, DnsRequest *&dns) {
		sockaddr_in target{};
		target.sin_family = AF_INET;
		target.sin_port = htons(number);
		if (!addressFor(name, target.sin_addr, version, dns) || interrupted(version))
			return -1;
		const int socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socket < 0)
			return -1;
		if (socket >= FD_SETSIZE || fcntl(socket, F_SETFL, O_NONBLOCK) < 0) {
			::close(socket);
			return -1;
		}
#if !defined(ARDUINO_ARCH_ESP32) || LWIP_SO_LINGER
		// Abort pending TCP data on stacks exposing SO_LINGER.
		linger abortive{1, 0};
		if (setsockopt(socket, SOL_SOCKET, SO_LINGER, &abortive, sizeof(abortive)) != 0) {
			::close(socket);
			return -1;
		}
#else
		// Stock Arduino 2/3 disable SO_LINGER. lwIP also uses SO_SNDTIMEO
		// to bound close retries when allocating a FIN fails (api_msg.c,
		// lwip_netconn_do_close_internal). Expiration is checked by TCP poll,
		// so teardown can additionally take a TCP poll interval under low RAM.
		timeval closeTimeout{0, 20000};
		if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &closeTimeout, sizeof(closeTimeout)) != 0) {
			::close(socket);
			return -1;
		}
#endif
		int result = ::connect(socket, reinterpret_cast<sockaddr *>(&target), sizeof(target));
		if (result < 0 && wouldBlock() && writable(socket, version, Clock::now() + timeout)) {
			int error = 0;
			socklen_t size = sizeof(error);
			result = getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0 ? 0 : -1;
		}
		if (result != 0 || interrupted(version)) {
			::close(socket);
			return -1;
		}
		return socket;
	}
	bool sendRecord(int socket, size_t length, uint32_t version) {
		const auto deadline = Clock::now() + timeout;
		const auto result = detail::writeRecord(
			record.get(), length,
			[socket](const char *data, size_t remaining) -> int {
#ifdef ARDUINO_ARCH_ESP32
				const int flags = MSG_DONTWAIT;
#else
				const int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif
				const int count = ::send(socket, data, remaining, flags);
				return count > 0 ? count : (count < 0 && wouldBlock() ? -1 : -2);
			},
			[&] { return writable(socket, version, deadline); }, [&] { return interrupted(version); }, [&] { return Clock::now() >= deadline; });
		partialWrites.fetch_add(result.partialWrites);
		if (result.sent == length)
			return true;
		if (result.timedOut)
			++writeTimeouts;
		if (result.error)
			++writeErrors;
		++lostRecords;
		lostBytes.fetch_add(length - result.sent);
		return false;
	}
	void run() {
		int socket = -1;
		uint32_t version = 0;
		char destination[254]{};
		uint16_t number = 0;
		DnsRequest *dns = nullptr;
		auto nextAttempt = Clock::now();
		bool hasConnected = false;
		while (!stop.load()) {
#ifdef ARDUINO_ARCH_ESP32
			const auto freeStack = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
			const auto previous = minimumFreeStack.load();
			if (!previous || freeStack < previous)
				minimumFreeStack.store(freeStack);
#endif
			if (generation.load() != version) {
				if (socket >= 0)
					::close(socket);
				socket = -1;
				connected.store(false);
				std::lock_guard<std::mutex> lock(mutex);
				version = generation.load();
				std::memcpy(destination, host, sizeof(host));
				number = port;
				nextAttempt = Clock::now();
			}
			if (socket < 0) {
#ifdef ARDUINO_ARCH_ESP32
				if (WiFi.status() != WL_CONNECTED) {
					wait(std::chrono::milliseconds(100));
					continue;
				}
#endif
				if (!destination[0] || !number || Clock::now() < nextAttempt) {
					wait(std::chrono::milliseconds(100));
					continue;
				}
				socket = open(destination, number, version, dns);
				if (socket < 0) {
					if (!interrupted(version)) {
						++connectionFailures;
						discard(version);
					}
					nextAttempt = Clock::now() + reconnectDelay;
					continue;
				}
				connected.store(true);
				if (hasConnected)
					++reconnects;
				hasConnected = true;
			}
			size_t length = 0;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (!interrupted(version))
					length = queue.pop(record.get());
			}
			if (!length) {
				wait(std::chrono::milliseconds(100));
				continue;
			}
			if (!sendRecord(socket, length, version)) {
				::close(socket);
				socket = -1;
				connected.store(false);
				discard(version);
				nextAttempt = Clock::now() + reconnectDelay;
			}
		}
		if (socket >= 0)
			::close(socket);
		if (dns)
			dns->release();
		{
			std::lock_guard<std::mutex> lock(mutex);
			loseQueue();
		}
		connected.store(false);
	}
};

void LoggerSocketSink::StateDeleter::operator()(State *state) const {
	detail::destroyInternal(state);
}

LoggerSocketSink::LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port, int32_t timeout, size_t capacity, size_t maximum) : LoggerSocketSink(level, std::move(hostName), port, MemoryPreference::InternalRam, timeout, capacity, 64 * 1024, maximum) {}

LoggerSocketSink::LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port, MemoryPreference preference, int32_t timeout, size_t capacity, size_t psramCapacity, size_t maximum) : state_(detail::makeInternal<State>(capacity, maximum, timeout, preference, psramCapacity)), level_(level) {
	if (!state_)
		return;
	auto &state = *state_;
	if (!state.record || !state.queue.capacity() || maximum + 2 > state.queue.capacity()) {
		++state.noResources;
		return;
	}
	state.publish(hostName, port);
#ifdef ARDUINO_ARCH_ESP32
	state.finished = xSemaphoreCreateBinary();
	if (!state.finished) {
		++state.noResources;
		return;
	}
	if (xTaskCreate(
			[](void *argument) {
				auto *state = static_cast<State *>(argument);
				state->run();
				{
					std::lock_guard<std::mutex> lock(state->shutdownMutex);
				}
				xSemaphoreGive(state->finished);
				vTaskDelete(nullptr);
			},
			"tcp-logger", workerStackBytes, &state, tskIDLE_PRIORITY + 1, &state.task) != pdPASS) {
		state.task = nullptr;
		++state.noResources;
		return;
	}
#else
	try {
		state.task = std::thread([&state] { state.run(); });
	} catch (...) {
		++state.noResources;
		return;
	}
#endif
	state.enabled.store(true);
}

LoggerSocketSink::~LoggerSocketSink() {
	if (!state_)
		return;
#ifdef ARDUINO_ARCH_ESP32
	{
		std::lock_guard<std::mutex> lock(state_->shutdownMutex);
		state_->stop.store(true);
		state_->wake();
	}
	// Cooperative exit: no task is deleted while holding locks or owning I/O.
	// Normally one 20 ms select slice; low-memory close adds a TCP poll
	// interval (typically <=2 s), plus scheduler/lwIP processing. No hard
	// scheduler bound is claimed; waiting for completion prevents use-after-free.
	if (state_->task)
		xSemaphoreTake(state_->finished, portMAX_DELAY);
	if (state_->finished)
		vSemaphoreDelete(state_->finished);
#else
	state_->stop.store(true);
	state_->wake();
	if (state_->task.joinable())
		state_->task.join();
#endif
}

void LoggerSocketSink::writeLog(const char *data, size_t length) {
	if (!length)
		return;
	if (!state_) {
		++initializationDroppedRecords_;
		initializationDroppedBytes_.fetch_add(length);
		return;
	}
	auto &state = *state_;
	if (!state.enabled.load() || !data)
		++state.noResources;
	else if (length > state.maxLength)
		++state.oversized;
	else {
		std::unique_lock<std::mutex> lock(state.mutex, std::try_to_lock);
		if (!lock.owns_lock())
			++state.lockBusy;
		else if (state.queue.push(data, length)) {
			++state.acceptedRecords;
			state.acceptedBytes.fetch_add(length);
			const bool becameNonempty = state.queue.records() == 1;
			lock.unlock();
			if (becameNonempty)
				state.wake();
			return;
		} else
			++state.bufferFull;
	}
	++state.droppedRecords;
	state.droppedBytes.fetch_add(length);
}

void LoggerSocketSink::setHost(std::string hostName, uint16_t port) {
	if (state_)
		state_->publish(hostName, port);
}

LoggerSocketSink::Statistics LoggerSocketSink::getStatistics() const {
	Statistics result;
	if (!state_) {
		result.droppedRecords = initializationDroppedRecords_.load();
		result.droppedBytes = initializationDroppedBytes_.load();
		result.noResources = 1 + result.droppedRecords;
		return result;
	}
#define COPY(name) result.name = state_->name.load();
	COUNTERS(COPY)
#undef COPY
#undef COUNTERS
	result.memoryType = state_->queue.memoryType();
	result.bufferCapacity = state_->queue.capacity();
	result.psramAllocationFailures = state_->queue.psramAllocationFailures();
	result.internalAllocationFailures = state_->queue.internalAllocationFailures();
	result.psramFallbacks = state_->queue.psramFallbacks();
	result.connected = state_->connected.load();
	result.enabled = state_->enabled.load();
	std::lock_guard<std::mutex> lock(state_->mutex);
	result.queuedBytes = state_->queue.used();
	result.peakQueuedBytes = state_->queue.highWater();
	return result;
}

} // namespace ib::logger
