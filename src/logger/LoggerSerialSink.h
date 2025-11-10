#pragma once
#include "LoggerInterface.h"
#include <mutex>

namespace ib::logger {


class LoggerSerialSink : public LoggerSinkInterface {
public:
	LoggerSerialSink(LoggerInterface::LogLevel level) : level_(level) {
	}

	LoggerInterface::LogLevel getLevel() const override {
		return level_;
	}

	void setLevel(LoggerInterface::LogLevel level) override {
		level_ = level;
	}

	void writeLog(const char* data, size_t length) const override;

private:
	LoggerInterface::LogLevel level_;
	mutable std::mutex mutex_;
};

}