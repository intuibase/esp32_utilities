#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif

namespace ib::logger {
enum class LogBufferMemoryPreference { InternalRam, PreferPsram };
enum class LogBufferMemoryType { None, InternalRam, Psram };

namespace detail {
// Hardware allocation is isolated here. Native builds deliberately report no
// PSRAM; tests can inject an allocator without pretending to test real hardware.
inline void *allocateLogMemory(size_t size, LogBufferMemoryType type) {
#ifdef ARDUINO_ARCH_ESP32
	if (type == LogBufferMemoryType::InternalRam)
		return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#ifdef MALLOC_CAP_SPIRAM
	if (type == LogBufferMemoryType::Psram)
		return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
	return nullptr;
#else
	return type == LogBufferMemoryType::InternalRam ? std::malloc(size) : nullptr;
#endif
}
inline void freeLogMemory(void *memory) {
#ifdef ARDUINO_ARCH_ESP32
	heap_caps_free(memory);
#else
	std::free(memory);
#endif
}
struct LogMemoryAllocator {
	void *context = nullptr;
	void *(*allocate)(void *, size_t, LogBufferMemoryType) = [](void *, size_t size, LogBufferMemoryType type) { return allocateLogMemory(size, type); };
	void (*free)(void *, void *) = [](void *, void *memory) { freeLogMemory(memory); };
};
struct LogMemoryDeleter {
	void *context = nullptr;
	void (*free)(void *, void *) = [](void *, void *memory) { freeLogMemory(memory); };
	void operator()(void *memory) const { free(context, memory); }
};

template <class T>
void destroyInternal(T *object) {
	if (object) {
		object->~T();
		freeLogMemory(object);
	}
}
template <class T, class... Args>
T *makeInternal(Args &&...args) {
	void *memory = allocateLogMemory(sizeof(T), LogBufferMemoryType::InternalRam);
	if (!memory)
		return nullptr;
#if defined(__cpp_exceptions)
	try {
		return new (memory) T(std::forward<Args>(args)...);
	} catch (...) {
		freeLogMemory(memory);
		return nullptr;
	}
#else
	return new (memory) T(std::forward<Args>(args)...);
#endif
}
} // namespace detail
} // namespace ib::logger
