#pragma once

#include <bitset>
#include <memory>
#include <string>
#include <unordered_map>

namespace ib::logger {

class LoggerSinkInterface;

class LoggerInterface {
public:
	enum class LogLevel { OFF, ERROR, WARN, INFO, DEBUG, TRACE };

	using LogFeature = std::bitset<255>;
	using LogFeatureType = uint8_t;

	virtual ~LoggerInterface() {}

	virtual void printf(LogLevel level, const char *format, ...) const = 0;
	virtual void printf(LogLevel level, LogFeatureType feature, const char *format, ...) const = 0;

	virtual bool doesMeetsLevelCondition(LogLevel level) const = 0;
	virtual bool doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const = 0;
	virtual LogLevel getMaxLogLevel() const = 0;
	virtual void setLogFeatures(LogFeature features) = 0;

	virtual LogFeatureType addFeature(std::string featureName) = 0;
	virtual std::string getFeatureName(LogFeatureType feature) const = 0;
	virtual std::unordered_map<LogFeatureType, std::string> getRegisteredFeatures() const = 0;

	virtual bool isFeatureEnabled(LogFeatureType feature) const = 0;
	virtual void enableFeature(LogFeatureType feature, bool enable) = 0;

	virtual void attachSink(std::shared_ptr<LoggerSinkInterface> sink) = 0;
};

class LoggerSinkInterface {
public:
	virtual ~LoggerSinkInterface() {}

	virtual LoggerInterface::LogLevel getLevel() const = 0;
	virtual void setLevel(LoggerInterface::LogLevel) = 0;

	virtual void writeLog(const char *data, size_t length) = 0;
};

} // namespace ib::logger

#define DBGLOG(log, level, ...)              \
	do {                                     \
		if (log) {                           \
			log->printf(level, __VA_ARGS__); \
		}                                    \
	} while (0)

#define DBGLOGI(log, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::INFO, __VA_ARGS__)
#define DBGLOGE(log, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::ERROR, __VA_ARGS__)
#define DBGLOGW(log, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::WARN, __VA_ARGS__)
#define DBGLOGD(log, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::DEBUG, __VA_ARGS__)
#define DBGLOGT(log, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::TRACE, __VA_ARGS__)

#define DBGLOGFI(log, feature, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::INFO, feature, __VA_ARGS__)
#define DBGLOGFE(log, feature, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::ERROR, feature, __VA_ARGS__)
#define DBGLOGFW(log, feature, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::WARN, feature, __VA_ARGS__)
#define DBGLOGFD(log, feature, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::DEBUG, feature, __VA_ARGS__)
#define DBGLOGFT(log, feature, ...) DBGLOG(log, ib::logger::LoggerInterface::LogLevel::TRACE, feature, __VA_ARGS__)