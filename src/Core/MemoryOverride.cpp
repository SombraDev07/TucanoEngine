// Global operator new/delete overrides — redirect to rpmalloc via g_allocGlobal.
// Compiled ONLY into the final executable (not into TucanoRuntime static lib)
// to avoid ODR violations. Add this source to each executable target that needs it.

#include "Core/Memory.h"

#include <new>

void* operator new(size_t size) {
	void* ptr = tucano::core::g_allocGlobal.alloc(size);
	if (!ptr) throw std::bad_alloc();
	return ptr;
}

void* operator new[](size_t size) {
	return operator new(size);
}

void* operator new(size_t size, std::align_val_t alignment) {
	void* ptr = tucano::core::g_allocGlobal.alloc(size, static_cast<size_t>(alignment));
	if (!ptr) throw std::bad_alloc();
	return ptr;
}

void* operator new[](size_t size, std::align_val_t alignment) {
	return operator new(size, alignment);
}

void operator delete(void* ptr) noexcept {
	tucano::core::g_allocGlobal.free(ptr);
}

void operator delete[](void* ptr) noexcept {
	operator delete(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
	tucano::core::g_allocGlobal.free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
	operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
	tucano::core::g_allocGlobal.free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
	operator delete(ptr);
}
