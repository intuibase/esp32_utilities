#pragma once

#include <bitset>
#include <string>

namespace ib::logger {

class LoggerInterface {
public:
	enum class LogLevel {
		OFF,
		ERROR,
		WARN,
		INFO,
		DEBUG,
		TRACE
	};

	using LogFeature = std::bitset<255>;
	using LogFeatureType = uint8_t;

	virtual ~LoggerInterface() {
	}

	virtual void printf(LogLevel level, const char *format, ...) const = 0;
	virtual bool doesMeetsLevelCondition(LogLevel level) const = 0;
	virtual bool doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const = 0;
	virtual LogLevel getMaxLogLevel() const = 0;
	virtual void setLogFeatures(LogFeature features) = 0;
};

class LoggerSinkInterface {
public:
	virtual ~LoggerSinkInterface() {
	}

	virtual LoggerInterface::LogLevel getLevel() const = 0;
	virtual void setLevel(LoggerInterface::LogLevel) = 0;

	virtual void writeLog(const char *data, size_t length) const = 0;
};

}