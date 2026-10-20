#pragma once

#include "LogMemory.h"
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
	explicit LogRecordBuffer(size_t internalCapacity, LogBufferMemoryPreference preference = LogBufferMemoryPreference::InternalRam, size_t psramCapacity = 64 * 1024, LogMemoryAllocator allocator = {}) : bytes_(nullptr, LogMemoryDeleter{allocator.context, allocator.free}) {
		auto attempt = [&](size_t capacity, LogBufferMemoryType type) {
			auto *memory = capacity >= 3 ? static_cast<uint8_t *>(allocator.allocate(allocator.context, capacity, type)) : nullptr;
			if (!memory)
				return false;
			bytes_.reset(memory);
			capacity_ = capacity;
			memoryType_ = type;
			return true;
		};
		if (preference == LogBufferMemoryPreference::PreferPsram) {
			if (attempt(psramCapacity, LogBufferMemoryType::Psram))
				return;
			++psramAllocationFailures_;
			++psramFallbacks_;
		}
		if (!attempt(internalCapacity, LogBufferMemoryType::InternalRam))
			++internalAllocationFailures_;
	}

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
	LogBufferMemoryType memoryType() const { return memoryType_; }
	uint32_t psramAllocationFailures() const { return psramAllocationFailures_; }
	uint32_t internalAllocationFailures() const { return internalAllocationFailures_; }
	uint32_t psramFallbacks() const { return psramFallbacks_; }

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
	std::unique_ptr<uint8_t, LogMemoryDeleter> bytes_;
	LogBufferMemoryType memoryType_ = LogBufferMemoryType::None;
	uint32_t psramAllocationFailures_ = 0, internalAllocationFailures_ = 0, psramFallbacks_ = 0;
	size_t capacity_ = 0, head_ = 0, tail_ = 0, used_ = 0, records_ = 0, highWater_ = 0;
};

} // namespace ib::logger::detail
