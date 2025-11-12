#include "LoggerSocketSink.h"
#include "TimeHelpers.h"

#include <WiFi.h>
#include <Arduino.h>

namespace ib::logger {

LoggerSocketSink::LoggerSocketSink(LoggerInterface::LogLevel level, std::string hostName, uint16_t port, int32_t connectionTimeoutMs) : client_(std::make_unique<WiFiClient>()), level_(level), hostName_(std::move(hostName)), port_(port), connectionTimeoutMs_(connectionTimeoutMs) {
	if (hostName_.empty()) {
		Serial.println("LoggerSocketSink host name is empty, not connecting");
		return;
	}

	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("LoggerSocketSink WiFi not connected, not connecting to host");
		return;
	}

	if (!client_->connect(hostName_.c_str(), port_, connectionTimeoutMs_)) {
		Serial.println("LoggerSocketSink connection to host failed");
	}
}

void LoggerSocketSink::connect() {
    if (hostName_.empty()) {
        return;
    }

	if (WiFi.status() != WL_CONNECTED) {
		return;
	}

    if (!client_->connected() && reconnectCounter_.durationPassed()) {
        Serial.printf("LoggerSocketSink reconnecting logger %s:%d\n", hostName_.c_str(), port_);
		if (!client_->connect(hostName_.c_str(), port_, connectionTimeoutMs_)) {
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