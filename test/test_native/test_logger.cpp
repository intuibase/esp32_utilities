#include <gtest/gtest.h>
#include "logger/Logger.h"

#include <string>
#include <vector>
#include <mutex>
#include <barrier>
#include <thread>

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
	ASSERT_EQ(lastLog.size(), 24 + longStr.size() + 1);
	EXPECT_EQ(lastLog.substr(24), longStr + "\n");
}

class LoggerBufferBoundary : public ::testing::TestWithParam<std::pair<bool, size_t>> {};

TEST_P(LoggerBufferBoundary, PreservesFullMessageAndExactLength) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	const auto [withFeature, totalLength] = GetParam();
	auto sink = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	ib::logger::Logger logger({sink});
	auto feature = logger.addFeature("Boundary");
	const std::string prefix = withFeature ? "[Boundary  ] " : "";
	// Timestamp is 24 bytes; leave room for a distinctive final byte.
	const std::string payload = std::string(totalLength - 24 - prefix.size() - 1, 'X') + '!';
	if (withFeature)
		logger.printf(LogLevel::INFO, feature, "%s", payload.c_str());
	else
		logger.printf(LogLevel::INFO, "%s", payload.c_str());

	ASSERT_EQ(sink->getLogs().size(), 1u);
	const auto &output = sink->getLogs().front();
	ASSERT_EQ(output.size(), totalLength);
	EXPECT_EQ(output.substr(24), prefix + payload);
}

INSTANTIATE_TEST_SUITE_P(LocalBuffer, LoggerBufferBoundary,
	::testing::Values(std::make_pair(false, size_t{199}), std::make_pair(false, size_t{200}),
		std::make_pair(false, size_t{201}), std::make_pair(true, size_t{199}),
		std::make_pair(true, size_t{200}), std::make_pair(true, size_t{201})));

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

TEST(Logger, AddFeatureTwiceReturnsSameId) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});
	auto f1 = logger.addFeature("Feature");
	auto f2 = logger.addFeature("Feature");
	EXPECT_EQ(f1, f2);
}

TEST(Logger, AddFeatureWithEnabledFlag) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});

	auto f1 = logger.addFeature("Feature1", true);
	auto f2 = logger.addFeature("Feature2", false);
	EXPECT_TRUE(logger.isFeatureEnabled(f1));
	EXPECT_FALSE(logger.isFeatureEnabled(f2));
	EXPECT_NE(f1, f2);
}

TEST(Logger, PlainPrintfAfterFirstFeatureDoesNotUseFeaturePrefix) {
	auto sink = std::make_shared<MockLoggerSink>(ib::logger::LoggerInterface::LogLevel::INFO);
	ib::logger::Logger logger({sink});
	logger.addFeature("FirstFeature");
	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO, "plain-message\n");
	EXPECT_TRUE(sink->hasLogContaining("plain-message"));
	EXPECT_EQ(sink->getLastLog().find("[FirstFeatu]"), std::string::npos);

	logger.printf(ib::logger::LoggerInterface::LogLevel::INFO,
		static_cast<ib::logger::LoggerInterface::LogFeatureType>(0), "feature-message\n");
	EXPECT_TRUE(sink->hasLogContaining("[FirstFeatu] feature-message"));
}

TEST(Logger, FeatureWithRejectedLevelIsNotFormatted) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	auto sink = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	ib::logger::Logger logger({sink});
	auto feature = logger.addFeature("Test");
	int formattedCharacters = -1;

	// %n makes formatting observable even when no sink receives the result.
	logger.printf(LogLevel::DEBUG, feature, "hidden%n", &formattedCharacters);
	EXPECT_EQ(formattedCharacters, -1);
	EXPECT_TRUE(sink->getLogs().empty());
}

TEST(Logger, SetLogFeaturesControlsFiltering) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	auto sink = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	ib::logger::Logger logger({sink});
	auto first = logger.addFeature("First");
	auto second = logger.addFeature("Second");
	ib::logger::LoggerInterface::LogFeature features;
	features.set(second);
	logger.setLogFeatures(features);

	EXPECT_FALSE(logger.isFeatureEnabled(first));
	EXPECT_FALSE(logger.doesFeatureMeetsLevelCondition(LogLevel::INFO, first));
	EXPECT_TRUE(logger.doesFeatureMeetsLevelCondition(LogLevel::INFO, second));
	logger.printf(LogLevel::INFO, first, "hidden\n");
	logger.printf(LogLevel::INFO, second, "visible\n");
	logger.printf(LogLevel::INFO, "plain\n");
	EXPECT_EQ(sink->getLogs().size(), 2u);
	EXPECT_TRUE(sink->hasLogContaining("visible"));
	EXPECT_TRUE(sink->hasLogContaining("plain"));
}

TEST(Logger, ConcurrentFeatureUpdatesAndReads) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	auto sink = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	ib::logger::Logger logger({sink});
	auto feature = logger.addFeature("Concurrent");
	std::barrier start(4);
	constexpr int iterations = 2000;
	std::thread toggler([&] {
		start.arrive_and_wait();
		for (int i = 0; i < iterations; ++i)
			logger.enableFeature(feature, i % 2 == 0);
	});
	std::thread replacer([&] {
		start.arrive_and_wait();
		for (int i = 0; i < iterations; ++i) {
			ib::logger::LoggerInterface::LogFeature features;
			features.set(feature, i % 2 == 0);
			logger.setLogFeatures(features);
		}
	});
	std::thread reader([&] {
		start.arrive_and_wait();
		for (int i = 0; i < iterations; ++i) {
			(void)logger.isFeatureEnabled(feature);
			(void)logger.doesFeatureMeetsLevelCondition(LogLevel::INFO, feature);
			logger.printf(LogLevel::INFO, feature, "concurrent entry\n");
		}
	});
	start.arrive_and_wait();
	toggler.join();
	replacer.join();
	reader.join();

	// No ordering is promised during updates; the final published mask must apply.
	logger.setLogFeatures({});
	EXPECT_FALSE(logger.isFeatureEnabled(feature));
	EXPECT_FALSE(logger.doesFeatureMeetsLevelCondition(LogLevel::INFO, feature));
	sink->clear();
	logger.printf(LogLevel::INFO, feature, "hidden\n");
	EXPECT_TRUE(sink->getLogs().empty());
	logger.enableFeature(feature, true);
	logger.printf(LogLevel::INFO, feature, "visible\n");
	EXPECT_TRUE(sink->hasLogContaining("visible"));
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

TEST(Logger, AttachDuringWriteAffectsOnlySubsequentEntries) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	class AttachingSink : public MockLoggerSink {
	public:
		AttachingSink() : MockLoggerSink(LogLevel::INFO) {}

		void writeLog(const char *data, size_t length) override {
			MockLoggerSink::writeLog(data, length);
			if (sinkToAttach) {
				logger->attachSink(std::move(sinkToAttach));
			}
		}

		ib::logger::Logger *logger = nullptr;
		std::shared_ptr<ib::logger::LoggerSinkInterface> sinkToAttach;
	};

	auto attaching = std::make_shared<AttachingSink>();
	auto existing = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	auto added = std::make_shared<MockLoggerSink>(LogLevel::DEBUG);
	ib::logger::Logger logger({attaching, existing});
	attaching->logger = &logger;
	attaching->sinkToAttach = added;

	logger.printf(LogLevel::INFO, "first entry\n");
	EXPECT_TRUE(existing->hasLogContaining("first entry"));
	EXPECT_TRUE(added->getLogs().empty());
	EXPECT_EQ(logger.getMaxLogLevel(), LogLevel::DEBUG);

	logger.printf(LogLevel::DEBUG, "second entry\n");
	EXPECT_TRUE(added->hasLogContaining("second entry"));
	EXPECT_EQ(attaching->getLogs().size(), 1u);
	EXPECT_EQ(existing->getLogs().size(), 1u);
}

TEST(Logger, ConcurrentAttachPreservesEverySinkWhileLogging) {
	using LogLevel = ib::logger::LoggerInterface::LogLevel;
	auto initial = std::make_shared<MockLoggerSink>(LogLevel::INFO);
	ib::logger::Logger logger({initial});
	std::vector<std::shared_ptr<MockLoggerSink>> first, second;
	for (int i = 0; i < 32; ++i) {
		first.push_back(std::make_shared<MockLoggerSink>(LogLevel::INFO));
		second.push_back(std::make_shared<MockLoggerSink>(LogLevel::INFO));
	}
	std::barrier start(4);
	auto attach = [&](const auto &sinks) {
		start.arrive_and_wait();
		for (const auto &sink : sinks)
			logger.attachSink(sink);
	};
	std::thread firstAttacher([&] { attach(first); });
	std::thread secondAttacher([&] { attach(second); });
	std::thread writer([&] {
		start.arrive_and_wait();
		for (int i = 0; i < 100; ++i)
			logger.printf(LogLevel::INFO, "during attach %d\n", i);
	});
	start.arrive_and_wait();
	firstAttacher.join();
	secondAttacher.join();
	writer.join();

	logger.printf(LogLevel::INFO, "after both attachers\n");
	EXPECT_TRUE(initial->hasLogContaining("after both attachers"));
	for (const auto &sinks : {first, second}) {
		for (const auto &sink : sinks)
			EXPECT_TRUE(sink->hasLogContaining("after both attachers"));
	}
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
