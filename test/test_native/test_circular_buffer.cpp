#include <gtest/gtest.h>
#include "CircularBuffer.h"

#include <string>

// ============================================================================
// Basic operations
// ============================================================================

TEST(CircularBuffer, EmptyOnConstruction) {
	ib::CircularBuffer<int, 5> buf;
	EXPECT_TRUE(buf.empty());
	EXPECT_EQ(buf.size(), 0u);
}

TEST(CircularBuffer, PushIncrementsSize) {
	ib::CircularBuffer<int, 5> buf;
	buf.push(10);
	EXPECT_FALSE(buf.empty());
	EXPECT_EQ(buf.size(), 1u);

	buf.push(20);
	EXPECT_EQ(buf.size(), 2u);
}

TEST(CircularBuffer, SizeNeverExceedsCapacity) {
	ib::CircularBuffer<int, 3> buf;
	for (int i = 0; i < 10; i++) {
		buf.push(i);
	}
	EXPECT_EQ(buf.size(), 3u);
}

// ============================================================================
// Access: get / operator[]
// ============================================================================

TEST(CircularBuffer, GetReturnsInInsertionOrder_BeforeWrap) {
	ib::CircularBuffer<int, 5> buf;
	buf.push(10);
	buf.push(20);
	buf.push(30);

	EXPECT_EQ(buf.get(0), 10);
	EXPECT_EQ(buf.get(1), 20);
	EXPECT_EQ(buf.get(2), 30);
}

TEST(CircularBuffer, OperatorBracketSameAsGet) {
	ib::CircularBuffer<int, 5> buf;
	buf.push(42);
	buf.push(99);

	EXPECT_EQ(buf[0], buf.get(0));
	EXPECT_EQ(buf[1], buf.get(1));
}

TEST(CircularBuffer, GetThrowsOnOutOfRange) {
	ib::CircularBuffer<int, 5> buf;
	EXPECT_THROW(buf.get(0), std::out_of_range);

	buf.push(1);
	EXPECT_THROW(buf.get(1), std::out_of_range);
	EXPECT_THROW(buf.get(100), std::out_of_range);
}

TEST(CircularBuffer, OperatorBracketThrowsOnOutOfRange) {
	ib::CircularBuffer<int, 5> buf;
	EXPECT_THROW(buf[0], std::out_of_range);
}

// ============================================================================
// newest()
// ============================================================================

TEST(CircularBuffer, NewestReturnsLastPushed) {
	ib::CircularBuffer<int, 5> buf;
	buf.push(10);
	EXPECT_EQ(buf.newest(), 10);

	buf.push(20);
	EXPECT_EQ(buf.newest(), 20);

	buf.push(30);
	EXPECT_EQ(buf.newest(), 30);
}

TEST(CircularBuffer, NewestThrowsWhenEmpty) {
	ib::CircularBuffer<int, 5> buf;
	EXPECT_THROW(buf.newest(), std::out_of_range);
}

TEST(CircularBuffer, NewestAfterWrap) {
	ib::CircularBuffer<int, 3> buf;
	buf.push(1);
	buf.push(2);
	buf.push(3);
	EXPECT_EQ(buf.newest(), 3);

	buf.push(4); // wraps around
	EXPECT_EQ(buf.newest(), 4);

	buf.push(5);
	EXPECT_EQ(buf.newest(), 5);
}

// ============================================================================
// Wraparound behavior
// ============================================================================

TEST(CircularBuffer, OverwritesOldestOnWrap) {
	ib::CircularBuffer<int, 3> buf;
	buf.push(1);
	buf.push(2);
	buf.push(3);
	// Buffer: [1, 2, 3], full

	buf.push(4);
	// Buffer now: [4, 2, 3] (physical), oldest overwritten

	EXPECT_EQ(buf.size(), 3u);
	EXPECT_EQ(buf.newest(), 4);
}

TEST(CircularBuffer, MultipleWraps) {
	ib::CircularBuffer<int, 2> buf;
	for (int i = 0; i < 100; i++) {
		buf.push(i);
		EXPECT_EQ(buf.newest(), i);
		EXPECT_LE(buf.size(), 2u);
	}
	EXPECT_EQ(buf.size(), 2u);
	EXPECT_EQ(buf.newest(), 99);
}

// ============================================================================
// getLastIndex()
// ============================================================================

TEST(CircularBuffer, GetLastIndex) {
	ib::CircularBuffer<int, 3> buf;
	buf.push(10);
	EXPECT_EQ(buf.getLastIndex(), 0u);

	buf.push(20);
	EXPECT_EQ(buf.getLastIndex(), 1u);

	buf.push(30);
	EXPECT_EQ(buf.getLastIndex(), 2u);

	buf.push(40); // wraps
	EXPECT_EQ(buf.getLastIndex(), 0u);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST(CircularBuffer, PushMoveSemantics) {
	ib::CircularBuffer<std::string, 3> buf;

	std::string s = "hello";
	buf.push(std::move(s));
	EXPECT_EQ(buf.newest(), "hello");
	// s should be moved from (implementation-defined, but typically empty)
}

TEST(CircularBuffer, PushCopy) {
	ib::CircularBuffer<std::string, 3> buf;

	std::string s = "world";
	buf.push(s);
	EXPECT_EQ(buf.newest(), "world");
	EXPECT_EQ(s, "world"); // original unchanged
}

// ============================================================================
// Capacity 1
// ============================================================================

TEST(CircularBuffer, CapacityOne) {
	ib::CircularBuffer<int, 1> buf;
	EXPECT_TRUE(buf.empty());

	buf.push(42);
	EXPECT_EQ(buf.size(), 1u);
	EXPECT_EQ(buf.newest(), 42);
	EXPECT_EQ(buf.get(0), 42);

	buf.push(99);
	EXPECT_EQ(buf.size(), 1u);
	EXPECT_EQ(buf.newest(), 99);
	EXPECT_EQ(buf.get(0), 99);
}
