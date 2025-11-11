#pragma once

#include "LoggerInterface.h"

#include <bitset>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace ib::logger {

class Logger : public LoggerInterface {
public:
	Logger(std::vector<std::shared_ptr<LoggerSinkInterface>> sinks) : sinks_(std::move(sinks)) {
	}

	void printf(LogLevel level, const char *format, ...) const override;
	void printf(LogLevel level, LogFeatureType feature, const char *format, ...) const override;
	void printf(LogLevel level, LogFeatureType feature, const char *format, va_list args) const;

	bool doesMeetsLevelCondition(LogLevel level) const override;
	bool doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const override;

	void attachSink(std::shared_ptr<LoggerSinkInterface> sink);

	LogLevel getMaxLogLevel() const override;

	void setLogFeatures(LogFeature features) override;

	LogFeatureType addFeature(std::string featureName);
	std::string getFeatureName(LogFeatureType feature) const;
	std::unordered_map<LogFeatureType, std::string> getRegisteredFeatures() const;

private:
	size_t getTime(char* buf, size_t size) const;

private:
	std::string getFormattedTime() const;
	std::string getFormattedProcessData() const;
	std::vector<std::shared_ptr<LoggerSinkInterface>> sinks_;
	LogFeature enabledFeatures_;
	std::unordered_map<LogFeatureType, std::string> registeredFeatures_;
	mutable std::shared_mutex mutex_;
	LogFeatureType lastFeatureId_ = 0;
};

}