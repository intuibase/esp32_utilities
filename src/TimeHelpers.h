#pragma once

#include <sys/time.h>

namespace ib {

void setTimeZone(const char *timeZone);
void setTimeZone(long offset, int daylight);

bool millisDurationPassed(unsigned long now, unsigned long lastJobTime, unsigned long interval);
bool millisDurationPassed(unsigned long lastJobTime, unsigned long interval);

uint64_t getTimeMillis();
}
