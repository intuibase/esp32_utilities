#include <gtest/gtest.h>
#include "logger/Logger.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

// Native linker wrapping injects failures without adding hooks to firmware.
#ifdef LOGGER_TEST_WRAP_C_LIB
namespace {
enum class Failure { None, FirstFormat, SecondFormat, SecondTruncated };
struct FormatProbe {
	Failure failure = Failure::None;
	int calls = 0;
	int allocations = 0;
	int frees = 0;
	void *buffer = nullptr;
};
thread_local FormatProbe probe;

class ScopedFormatFailure {
public:
	explicit ScopedFormatFailure(Failure failure) { probe = {failure}; }
	~ScopedFormatFailure() { probe.failure = Failure::None; }
};

class CountingSink : public ib::logger::LoggerSinkInterface {
public:
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	LogLevel getLevel() const override { return LogLevel::INFO; }
	void setLevel(LogLevel) override {}
	void writeLog(const char *, size_t) override { ++writes; }
	int writes = 0;
};
} // namespace

extern "C" int __real_vsnprintf(char *, size_t, const char *, va_list);
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);

extern "C" int __wrap_vsnprintf(char *out, size_t capacity, const char *format, va_list args) {
	if (probe.failure != Failure::None) {
		++probe.calls;
		if (probe.failure == Failure::FirstFormat && probe.calls == 1)
			return -1;
		if (probe.calls == 2) {
			if (probe.failure == Failure::SecondFormat)
				return -1;
			if (probe.failure == Failure::SecondTruncated)
				return static_cast<int>(capacity);
		}
	}
	return __real_vsnprintf(out, capacity, format, args);
}

extern "C" void *__wrap_malloc(size_t size) {
	void *result = __real_malloc(size);
	if (probe.failure != Failure::None) {
		++probe.allocations;
		probe.buffer = result;
	}
	return result;
}

extern "C" void __wrap_free(void *buffer) {
	if (probe.failure != Failure::None && buffer && buffer == probe.buffer)
		++probe.frees;
	__real_free(buffer);
}

namespace {
class LoggerFormatFailure : public ::testing::TestWithParam<Failure> {};

TEST_P(LoggerFormatFailure, DropsEntryAndReleasesBufferBeforeNextEntry) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	auto sink = std::make_shared<CountingSink>();
	ib::logger::Logger logger({sink});
	const std::string payload(300, 'X');
	{
		ScopedFormatFailure failure(GetParam());
		logger.printf(LogLevel::INFO, "%s", payload.c_str());
	}
	const bool firstCallFailed = GetParam() == Failure::FirstFormat;
	EXPECT_EQ(probe.calls, firstCallFailed ? 1 : 2);
	EXPECT_EQ(probe.allocations, firstCallFailed ? 0 : 1);
	EXPECT_EQ(probe.frees, probe.allocations);
	EXPECT_EQ(sink->writes, 0);

	logger.printf(LogLevel::INFO, "next entry\n");
	EXPECT_EQ(sink->writes, 1);
}

INSTANTIATE_TEST_SUITE_P(FormattingErrors, LoggerFormatFailure,
	::testing::Values(Failure::FirstFormat, Failure::SecondFormat, Failure::SecondTruncated));
} // namespace
#endif
