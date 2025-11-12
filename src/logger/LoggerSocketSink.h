#pragma once
#include "LoggerInterface.h"
#include "PeriodicCounter.h"
#include <mutex>
#include <string>
#include <memory>

class WiFiClient;

namespace ib::logger {

class LoggerSocketSink : public LoggerSinkInterface {
public:
	LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port, int32_t connectionTimeoutMs = 1000);

	LoggerInterface::LogLevel getLevel() const override {
		return level_;
	}

	void setLevel(LoggerInterface::LogLevel level) override {
		level_ = level;
	}

	void writeLog(const char* data, size_t length) override;


    void setHost(std::string hostName, uint16_t port) {
        std::lock_guard<std::mutex> lock(mutex_);
        hostName_ = std::move(hostName);
        port_ = port;
    }

private:
    void connect() ;

    std::unique_ptr<WiFiClient> client_;
	LoggerInterface::LogLevel level_;
    std::string hostName_;
    uint16_t port_;
	int32_t connectionTimeoutMs_ = 1000;
	PeriodicCounter reconnectCounter_{10000};
	mutable std::mutex mutex_;
};

}