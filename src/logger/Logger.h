#pragma once

#include "LoggerInterface.h"

#include <bitset>
#include <memory>
#include <mutex>
#include <vector>

namespace ib::logger {

class Logger : public LoggerInterface {
public:
	Logger(std::vector<std::shared_ptr<LoggerSinkInterface>> sinks) : sinks_(std::move(sinks)) {
	}

	void printf(LogLevel level, const char *format, ...) const override;

	bool doesMeetsLevelCondition(LogLevel level) const override;
	bool doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const override;

	void attachSink(std::shared_ptr<LoggerSinkInterface> sink);

	LogLevel getMaxLogLevel() const override;

	void setLogFeatures(LogFeature features) override;

private:
	size_t getTime(char* buf, size_t size) const;

private:
	std::string getFormattedTime() const;
	std::string getFormattedProcessData() const;
	std::vector<std::shared_ptr<LoggerSinkInterface>> sinks_;
	LogFeature features_;
	mutable std::mutex mutex_;
};

}