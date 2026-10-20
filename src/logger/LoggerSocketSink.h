#pragma once
#include "LoggerInterface.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ib::logger {

// Only this sink is asynchronous. Logger formatting and other sinks retain their
// existing behavior. Callers must stop using the sink before its destruction.
class LoggerSocketSink : public LoggerSinkInterface {
public:
	struct Statistics {
		// Monotonic uint32_t counters wrap modulo 2^32. A snapshot is approximate.
		uint32_t acceptedRecords = 0, acceptedBytes = 0;
		uint32_t droppedRecords = 0, droppedBytes = 0;
		uint32_t bufferFull = 0, oversized = 0, lockBusy = 0, noResources = 0;
		uint32_t partialWrites = 0, writeErrors = 0, writeTimeouts = 0;
		uint32_t connectionFailures = 0, reconnects = 0;
		// Accepted records subsequently abandoned; bytes count the unsent suffix.
		uint32_t lostRecords = 0, lostBytes = 0;
		// Queue occupancy includes headers, excludes the worker's in-flight record.
		// ESP-IDF reports stack high-water marks in bytes; native reports zero.
		uint32_t queuedBytes = 0, peakQueuedBytes = 0, minimumFreeStack = 0;
		bool connected = false, enabled = false;
	};

	// queueCapacity includes two bytes of metadata per record. One additional
	// maxRecordLength buffer belongs to the worker. ESP32 worker stack: 4096 B.
	// DNS/connect and each complete record have separate, non-renewing deadlines
	// of connectionTimeoutMs (clamped to 1..10000 ms). IPv4 destinations supported.
	LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port, int32_t connectionTimeoutMs = 1000, size_t queueCapacity = 2048, size_t maxRecordLength = 512);
	~LoggerSocketSink() override;
	LoggerSocketSink(const LoggerSocketSink &) = delete;
	LoggerSocketSink &operator=(const LoggerSocketSink &) = delete;

	LoggerInterface::LogLevel getLevel() const override { return level_.load(); }
	void setLevel(LoggerInterface::LogLevel level) override { level_.store(level); }
	void writeLog(const char *data, size_t length) override;
	// Publishes bounded configuration, drops the previous destination's backlog,
	// and wakes the worker. Empty or >253-byte hosts disable connection attempts.
	void setHost(std::string hostName, uint16_t port);
	Statistics getStatistics() const;

private:
	struct State;
	std::unique_ptr<State> state_;
	std::atomic<LoggerInterface::LogLevel> level_;
	// Keep drop accounting available even if allocating State itself failed.
	std::atomic<uint32_t> initializationDroppedRecords_{0}, initializationDroppedBytes_{0};
};

} // namespace ib::logger
