
#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ib::logger {

void Logger::setLogFeatures(LogFeature features) {
	features_ = std::move(features);
}

bool Logger::doesFeatureMeetsLevelCondition(LogLevel level, LogFeatureType feature) const {
	if (features_.test(feature)) {
		return doesMeetsLevelCondition(level);
	}
	return false;
}

ib::logger::LoggerInterface::LogLevel Logger::getMaxLogLevel() const {
	auto maxLevel = LogLevel::OFF;
	for (auto const &sink : sinks_) {
		maxLevel = std::max(sink->getLevel(), maxLevel);
	}
	return maxLevel;
}

bool Logger::doesMeetsLevelCondition(LogLevel level) const {
	auto maxLevel = LogLevel::OFF;
	for (auto const &sink : sinks_) {
		maxLevel = std::max(sink->getLevel(), maxLevel);
	}
	return level <= maxLevel;
}

void Logger::attachSink(std::shared_ptr<LoggerSinkInterface> sink) {
	std::lock_guard<std::mutex> lock(mutex_);
	sinks_.emplace_back(std::move(sink));
}

void Logger::printf(LogLevel level, const char *format, ...) const {
	char loc_buf[200];
	auto timeLen = getTime(loc_buf, 21);

	char *temp = loc_buf;
	va_list arg;
	va_list copy;
	va_start(arg, format);
	va_copy(copy, arg);
	int len = vsnprintf(temp + timeLen, sizeof(loc_buf) - timeLen, format, copy);
	va_end(copy);
	if (len < 0) {
		va_end(arg);
		return; // Encoding error
	}

	if (len >= (int)sizeof(loc_buf) - timeLen) { // comparation of same sign type for the compiler
		temp = (char *)malloc(len + 1 + timeLen);
		if (temp == NULL) {
			va_end(arg);
			return;// Allocation failed
		}
		memcpy(temp, loc_buf, timeLen);
		len = vsnprintf(temp + timeLen, len + 1, format, arg);
	}
	va_end(arg);

	std::vector<std::shared_ptr<LoggerSinkInterface>> sinksCopy;
	{
		std::lock_guard<std::mutex> lock(mutex_);
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