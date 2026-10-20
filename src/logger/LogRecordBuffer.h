#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace ib::logger::detail {

// Caller provides synchronization. Capacity includes a two-byte length per record.
// No allocation after construction; records may wrap at any byte boundary.
class LogRecordBuffer {
public:
	explicit LogRecordBuffer(size_t capacity) : bytes_(capacity >= 3 ? new (std::nothrow) uint8_t[capacity] : nullptr), capacity_(bytes_ ? capacity : 0) {}

	bool push(const char *data, size_t length) {
		if (!length || length > UINT16_MAX || length + 2 > capacity_ - used_)
			return false;
		const uint16_t size = static_cast<uint16_t>(length);
		append(&size, sizeof(size));
		append(data, length);
		++records_;
		highWater_ = std::max(highWater_, used_);
		return true;
	}

	size_t pop(char *data) {
		if (!records_)
			return 0;
		uint16_t length;
		read(&length, sizeof(length));
		read(data, length);
		--records_;
		return length;
	}

	void clear() { head_ = tail_ = used_ = records_ = 0; }
	size_t capacity() const { return capacity_; }
	size_t used() const { return used_; }
	size_t records() const { return records_; }
	size_t payloadBytes() const { return used_ - records_ * 2; }
	size_t highWater() const { return highWater_; }

private:
	void append(const void *source, size_t size) {
		const auto *data = static_cast<const uint8_t *>(source);
		const size_t first = std::min(size, capacity_ - tail_);
		std::memcpy(bytes_.get() + tail_, data, first);
		std::memcpy(bytes_.get(), data + first, size - first);
		tail_ = (tail_ + size) % capacity_;
		used_ += size;
	}
	void read(void *target, size_t size) {
		auto *data = static_cast<uint8_t *>(target);
		const size_t first = std::min(size, capacity_ - head_);
		std::memcpy(data, bytes_.get() + head_, first);
		std::memcpy(data + first, bytes_.get(), size - first);
		head_ = (head_ + size) % capacity_;
		used_ -= size;
	}
	std::unique_ptr<uint8_t[]> bytes_;
	size_t capacity_, head_ = 0, tail_ = 0, used_ = 0, records_ = 0, highWater_ = 0;
};

} // namespace ib::logger::detail
