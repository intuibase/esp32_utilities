#include "LoggerSocketSink.h"
#include "TimeHelpers.h"

#include <WiFi.h>
#include <Arduino.h>

namespace ib::logger {

LoggerSocketSink::LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port) : client_(std::make_unique<WiFiClient>()), level_(level), hostName_(std::move(hostName)), port_(port) {
    if (hostName_.empty()) {
        return;
    }

    if (!client_->connect(hostName_.c_str(), port_, 300)) {
        Serial.println("LoggerSocketSink connection to host failed");
    }
}


void LoggerSocketSink::connect() {
    if (hostName_.empty()) {
        return;
    }

    if (!client_->connected() && reconnectCounter_.durationPassed()) {
        Serial.printf("LoggerSocketSink reconnecting logger %s:%d\n", hostName_.c_str(), port_);
        if (!client_->connect(hostName_.c_str(), port_, 1000)) {
            Serial.println("LoggerSocketSink connection to host failed");
        }
    }
}



void LoggerSocketSink::writeLog(const char* data, size_t length)  {
	std::lock_guard<std::mutex> lock(mutex_);
	connect();
	client_->write(data, length);
}


}