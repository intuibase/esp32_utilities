#include "LoggerSerialSink.h"

#include <Arduino.h>

namespace ib::logger {

void LoggerSerialSink::writeLog(const char *data, size_t length) {
	std::lock_guard<std::mutex> lock(mutex_);
	Serial.write(data, length);
}
}