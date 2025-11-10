#include <Arduino.h>
#include "config.h"

#include <cstdio>
#include <cstdlib>


extern "C" {
int setenv(const char *__string, const char *__value, int __overwrite);
}

namespace ib {

void setTimeZone(long offset, int daylight) {
	char cst[17] = {0};
	char cdt[17] = "DST";
	char tz[33] = {0};

	if(offset % 3600) {
		sprintf(cst, "UTC%ld:%02lu:%02lu", offset / 3600, std::abs((offset % 3600) / 60), std::abs(offset % 60));
	} else {
		sprintf(cst, "UTC%ld", offset / 3600);
	}
	if(daylight != 3600){
		long tz_dst = offset - daylight;
		if(tz_dst % 3600){
			sprintf(cdt, "DST%ld:%02lu:%02lu", tz_dst / 3600, std::abs((tz_dst % 3600) / 60), std::abs(tz_dst % 60));
		} else {
			sprintf(cdt, "DST%ld", tz_dst / 3600);
		}
	}
	sprintf(tz, "%s%s", cst, cdt);
	setenv("TZ", tz, 1);
}


void setTimeZone(const char *timeZone) {
	setenv("TZ", timeZone, 1);
}

bool millisDurationPassed(unsigned long now, unsigned long lastJobTime, unsigned long interval) {
	if (now - lastJobTime < interval) {
		return false;
	}
	return true;
}

bool millisDurationPassed(unsigned long lastJobTime, unsigned long interval) {
	auto now = millis();
	return millisDurationPassed(now, lastJobTime, interval);
}

uint64_t getTimeMillis() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

}