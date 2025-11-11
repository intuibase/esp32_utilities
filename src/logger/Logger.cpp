#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ib::logger {

LoggerInterface::LogFeatureType Logger::addFeature(std::string featureName) {
	std::unique_lock<std::shared_mutex> lock(mutex_);
	auto featureId = lastFeatureId_++;
	registeredFeatures_.emplace(featureId, std::move(featureName));
	enabledFeatures_.set(featureId);
	return featureId;
}

std::string Logger::getFeatureName(LogFeatureType feature) const {
	std::shared_lock<std::shared_mutex> lock(mutex_);
	auto it = registeredFeatures_.find(feature);
	if (it != registeredFeatures_.end()) {
		return it->second;
	}
	return {};
}

std::unordered_map<LoggerInterface::LogFeatureType, std::string> Logger::getRegisteredFeatures() const {
	std::shared_lock<std::shared_mutex> lock(mutex_);
	return registeredFeatures_;
}

void Logger::setLogFeatures(LogFeature features) {
	enabledFeatures_ = std::move(features);
}

bool Logger::doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const {
	if (enabledFeatures_.test(feature)) {
		return doesMeetsLevelCondition(level);
	}
	return false;
}

ib::logger::LoggerInterface::LogLevel Logger::getMaxLogLevel() const {
	auto maxLevel = LogLevel::OFF;
	std::shared_lock<std::shared_mutex> lock(mutex_);
	for (auto const &sink : sinks_) {
		maxLevel = std::max(sink->getLevel(), maxLevel);
	}
	return maxLevel;
}

bool Logger::doesMeetsLevelCondition(LogLevel level) const {
	auto maxLevel = LogLevel::OFF;
	std::shared_lock<std::shared_mutex> lock(mutex_);
	for (auto const &sink : sinks_) {
		maxLevel = std::max(sink->getLevel(), maxLevel);
	}
	return level <= maxLevel;
}

void Logger::attachSink(std::shared_ptr<LoggerSinkInterface> sink) {
	std::unique_lock<std::shared_mutex> lock(mutex_);
	sinks_.emplace_back(std::move(sink));
}

void Logger::printf(LogLevel level, const char *format, ...) const {
	if (!doesMeetsLevelCondition(level)) {
		return;
	}

	va_list args;
	va_start(args, format);
	printf(level, 0, format, args);
	va_end(args);
}

void Logger::printf(LogLevel level, LogFeatureType feature, const char *format, ...) const {
	if (!enabledFeatures_.test(feature)) {
		return;
	}

	va_list args;
	va_start(args, format);
	printf(level, feature, format, args);
	va_end(args);
}

void Logger::printf(LogLevel level, LogFeatureType feature, const char *format, va_list args) const {
	char loc_buf[200];
	auto timeLen = getTime(loc_buf, 21);

	auto featureName = getFeatureName(feature);
	if (!featureName.empty()) {
		int written = snprintf(loc_buf + timeLen, sizeof(loc_buf) - timeLen, "[%-10.10s] ", featureName.c_str());
		if (written > 0) {
			timeLen += static_cast<size_t>(written);
		}
	}

	char *temp = loc_buf;
	va_list copy;

	va_copy(copy, args);
	int len = vsnprintf(temp + timeLen, sizeof(loc_buf) - timeLen, format, copy);
	va_end(copy);

	if (len < 0) {
		return; // Encoding error
	}

	if (len >= (int)sizeof(loc_buf) - timeLen) { // comparation of same sign type for the compiler
		temp = (char *)malloc(len + 1 + timeLen);
		if (temp == NULL) {
			return; // Allocation failed
		}
		memcpy(temp, loc_buf, timeLen);
		len = vsnprintf(temp + timeLen, len + 1, format, args);
	}

	std::vector<std::shared_ptr<LoggerSinkInterface>> sinksCopy;
	{
		std::shared_lock<std::shared_mutex> lock(mutex_);
		sinksCopy = sinks_;
	}

	for (auto const &sink : sinksCopy) {
		if (sink->getLevel() < level) {
			continue;
		}

		sink->writeLog(temp, len + timeLen);
	}

	if (temp != loc_buf) {
		free(temp);
	}
}

size_t Logger::getTime(char *buf, size_t size) const {
	struct timeval tv;
	gettimeofday(&tv, nullptr);

	time_t nowtime = tv.tv_sec;
	auto timeinfo = localtime(&nowtime);

	auto len = strftime(buf, size, "%Y-%m-%d %H:%M:%S.", timeinfo);
	sprintf(buf + len, "%3.3ld ", tv.tv_usec / 1000);
	return len + 4;
}

}