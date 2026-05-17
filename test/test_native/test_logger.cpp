#include <gtest/gtest.h>
#include "logger/Logger.h"

#include <string>
#include <vector>
#include <mutex>

// ============================================================================
// Mock sink for capturing log output
// ============================================================================

class MockLoggerSink : public ib::logger::LoggerSinkInterface {
public:
	MockLoggerSink(ib::logger::LoggerInterface::LogLevel level) : level_(level) {}

	ib::logger::LoggerInterface::LogLevel getLevel() const override {
		return level_;
	}

	void setLevel(ib::logger::LoggerInterface::LogLevel level) override {
		level_ = level;
	}

	void writeLog(const char *data, size_t length) override {
		std::lock_guard<std::mutex> lock(mutex_);
		logs_.emplace_back(data, length);
	}

	std::vector<std::string> const &getLogs() const { return logs_; }
	void clear() { logs_.clear(); }

	std::string getLastLog() const {
		if (logs_.empty()) return {};
		return logs_.back();
	}

	bool hasLogContaining(std::string_view text) const {
		for (auto const &log : logs_) {
			if (log.find(text) != std::string::npos) return true;
		}
		return false;
	}

private:
	ib::logger::LoggerInterface::LogLevel level_;
	std::vector<std::string> logs_;
	std::mutex mutex_;
};

// ============================================================================
// Logger construction
// ============================================================================

TEST(Logger, ConstructsWithNoSinks) {
	ib::logger::Logger logger({});
	// Should not crash
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "test %d\n", 42);
}

TEST(Logger, ConstructsWithSink) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	EXPECT_TRUE(logger.doesMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::INFO));
}

// ============================================================================
// Log level filtering
// ============================================================================

TEST(Logger, DoesNotLogBelowLevel) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::WARN);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "should not appear\n");
	EXPECT_TRUE(sink->getLogs().empty());
}

TEST(Logger, LogsAtExactLevel) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::WARN);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::WARN, "warning msg\n");
	EXPECT_FALSE(sink->getLogs().empty());
	EXPECT_TRUE(sink->hasLogContaining("warning msg"));
}

TEST(Logger, LogsAboveLevel) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::ERROR, "error msg\n");
	EXPECT_TRUE(sink->hasLogContaining("error msg"));
}

// ============================================================================
// Max log level
// ============================================================================

TEST(Logger, MaxLogLevel_SingleSink) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::DEBUG);
	ib::logger::Logger logger({sink});

	EXPECT_EQ(logger.getMaxLogLevel(), ib::logger::LoggerInterface::LogLevel::DEBUG);
}

TEST(Logger, MaxLogLevel_MultipleSinks) {
	auto sinkWarn = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::WARN);
	auto sinkTrace = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::TRACE);
	ib::logger::Logger logger({sinkWarn, sinkTrace});

	EXPECT_EQ(logger.getMaxLogLevel(), ib::logger::LoggerInterface::LogLevel::TRACE);
}

TEST(Logger, MaxLogLevel_NoSinks) {
	ib::logger::Logger logger({});
	EXPECT_EQ(logger.getMaxLogLevel(), ib::logger::LoggerInterface::LogLevel::OFF);
}

// ============================================================================
// Multiple sinks
// ============================================================================

TEST(Logger, WritesToAllEligibleSinks) {
	auto sinkInfo = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	auto sinkError = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::ERROR);
	ib::logger::Logger logger({sinkInfo, sinkError});

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "info msg\n");
	EXPECT_TRUE(sinkInfo->hasLogContaining("info msg"));
	EXPECT_TRUE(sinkError->getLogs().empty()); // INFO < ERROR level, so sink rejects

	logger.printf(ib::logger::LoggerInterface::LogLevel::ERROR, "error msg\n");
	EXPECT_TRUE(sinkInfo->hasLogContaining("error msg"));
	EXPECT_TRUE(sinkError->hasLogContaining("error msg"));
}

// ============================================================================
// Formatting
// ============================================================================

TEST(Logger, FormatsIntegers) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "value=%d\n", 42);
	EXPECT_TRUE(sink->hasLogContaining("value=42"));
}

TEST(Logger, FormatsStrings) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "name=%s\n", "test");
	EXPECT_TRUE(sink->hasLogContaining("name=test"));
}

TEST(Logger, LargeFormatString) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	// Generate a string longer than the 200-byte local buffer
	std::string longStr(300, 'X');
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "%s\n", longStr.c_str());
	std::string const lastLog = sink->getLastLog();
	std::string const tail = longStr.substr(longStr.size() - 50);
	EXPECT_NE(lastLog.find(tail), std::string::npos);
}

// ============================================================================
// Feature system
// ============================================================================

TEST(Logger, AddFeatureReturnsUniqueIds) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f1 = logger.addFeature("Feature1");
	auto f2 = logger.addFeature("Feature2");
	EXPECT_NE(f1, f2);
}

TEST(Logger, PlainPrintfAfterFirstFeatureDoesNotUseFeaturePrefix) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});
	logger.addFeature("FirstFeature");
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "plain-message\n");
	EXPECT_TRUE(sink->hasLogContaining("plain-message"));
	EXPECT_EQ(sink->getLastLog().find("FirstFeature"), std::string::npos);
}

TEST(Logger, GetFeatureName) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("MyFeature");
	EXPECT_EQ(logger.getFeatureName(f), "MyFeature");
}

TEST(Logger, FeatureEnabledByDefault) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	EXPECT_TRUE(logger.isFeatureEnabled(f));
}

TEST(Logger, DisableFeature) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	logger.enableFeature(f, false);
	EXPECT_FALSE(logger.isFeatureEnabled(f));
}

TEST(Logger, DisabledFeatureDoesNotLog) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	logger.enableFeature(f, false);

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, f, "should not log\n");
	EXPECT_FALSE(sink->hasLogContaining("should not log"));
}

TEST(Logger, EnabledFeatureLogs) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, f, "feature msg\n");
	EXPECT_TRUE(sink->hasLogContaining("feature msg"));
}

TEST(Logger, FeatureNameInOutput) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("MQTT");
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, f, "connected\n");
	// Feature name should be included in the log output prefix
	EXPECT_TRUE(sink->hasLogContaining("MQTT"));
}

TEST(Logger, GetRegisteredFeatures) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f1 = logger.addFeature("A");
	auto f2 = logger.addFeature("B");

	auto features = logger.getRegisteredFeatures();
	EXPECT_EQ(features.size(), 2u);
	EXPECT_EQ(features[f1], "A");
	EXPECT_EQ(features[f2], "B");
}

// ============================================================================
// doesFeatureMeetsLevelCondition
// ============================================================================

TEST(Logger, FeatureMeetsLevelCondition) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	EXPECT_TRUE(logger.doesFeatureMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::INFO, f));
	EXPECT_FALSE(logger.doesFeatureMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::DEBUG, f));
}

TEST(Logger, DisabledFeatureFailsMeetsCondition) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::TRACE);
	ib::logger::Logger logger({sink});

	auto f = logger.addFeature("Test");
	logger.enableFeature(f, false);
	EXPECT_FALSE(logger.doesFeatureMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::ERROR, f));
}

// ============================================================================
// attachSink
// ============================================================================

TEST(Logger, AttachSinkAfterConstruction) {
	ib::logger::Logger logger({});
	EXPECT_EQ(logger.getMaxLogLevel(), ib::logger::LoggerInterface::LogLevel::OFF);

	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::DEBUG);
	logger.attachSink(sink);
	EXPECT_EQ(logger.getMaxLogLevel(), ib::logger::LoggerInterface::LogLevel::DEBUG);

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "after attach\n");
	EXPECT_TRUE(sink->hasLogContaining("after attach"));
}

// ============================================================================
// Sink level change
// ============================================================================

TEST(Logger, ChangeSinkLevel) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::ERROR);
	ib::logger::Logger logger({sink});

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "hidden\n");
	EXPECT_TRUE(sink->getLogs().empty());

	sink->setLevel(ib::logger::LoggerInterface::LogLevel::INFO);
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "visible\n");
	EXPECT_TRUE(sink->hasLogContaining("visible"));
}
