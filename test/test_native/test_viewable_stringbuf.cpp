#include <gtest/gtest.h>
#include "viewable_stringbuf.h"

#include <ostream>
#include <string>

// ============================================================================
// Basic usage
// ============================================================================

TEST(ViewableStringbuf, EmptyOnConstruction) {
	ib::viewable_stringbuf buf;
	EXPECT_TRUE(buf.view().empty());
}

TEST(ViewableStringbuf, ViewAfterWrite) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "hello";
	EXPECT_EQ(buf.view(), "hello");
}

TEST(ViewableStringbuf, ViewAfterMultipleWrites) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "hello";
	ss << " ";
	ss << "world";
	EXPECT_EQ(buf.view(), "hello world");
}

TEST(ViewableStringbuf, ViewWithNumbers) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "val=" << 42 << " pi=" << 3.14;
	auto v = buf.view();
	EXPECT_NE(v.find("val=42"), std::string::npos);
	EXPECT_NE(v.find("pi=3.14"), std::string::npos);
}

TEST(ViewableStringbuf, ViewIsZeroCopy) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "test data";
	auto v1 = buf.view();
	auto v2 = buf.view();
	// Both views should point to the same buffer
	EXPECT_EQ(v1.data(), v2.data());
	EXPECT_EQ(v1, v2);
}

// ============================================================================
// Used in JSON-like patterns (like in MQTT.h)
// ============================================================================

TEST(ViewableStringbuf, JsonPattern) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "{\"name\": \"sensor1\", \"value\": " << 25 << "}";
	EXPECT_EQ(buf.view(), "{\"name\": \"sensor1\", \"value\": 25}");
}

TEST(ViewableStringbuf, LargePayload) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	for (int i = 0; i < 1000; i++) {
		ss << "data" << i << " ";
	}
	auto v = buf.view();
	EXPECT_EQ(v.size(), 7890u);
	EXPECT_EQ(v.substr(0, 5), "data0");
	EXPECT_EQ(v.substr(v.size() - 8), "data999 ");
}

// ============================================================================
// string_view properties
// ============================================================================

TEST(ViewableStringbuf, ViewLengthMatchesContent) {
	ib::viewable_stringbuf buf;
	std::ostream ss(&buf);

	ss << "12345";
	EXPECT_EQ(buf.view().length(), 5u);

	ss << "67890";
	EXPECT_EQ(buf.view().length(), 10u);
}
