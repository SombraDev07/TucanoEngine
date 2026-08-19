#include "Core/Memory.h"

#include <rpmalloc.h>

#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace tucano::core {

// ── Initialization ──

void memoryInit() {
	// Idempotent: rpmalloc_initialize() is not safe to call twice, and the callers that need it are
	// spread across window creation, job workers and headless tools. Guarding here means no caller
	// has to know whether someone else got there first.
	static std::once_flag once;
	std::call_once(once, [] { rpmalloc_initialize(); });
}

void memoryShutdown() {
	rpmalloc_finalize();
}

void memoryInitThreadHeap() {
	// rpmalloc_thread_initialize() on an uninitialised allocator faults. That is reachable without
	// anything obviously wrong at the call site: `memoryInit()` used to run only from window
	// creation, so any headless program that built an ecs::World crashed the moment JobSystem
	// started its workers — which is exactly how TucanoECSTest was failing.
	memoryInit();
	rpmalloc_thread_initialize();
}

bool memoryHasThreadHeap() {
	return rpmalloc_is_thread_initialized();
}

// ── Named allocator ──

MemoryAllocator::MemoryAllocator(const char* name) : m_name(name) {}

void* MemoryAllocator::alloc(size_t size, size_t alignment) {
	void* ptr = alignment > 16 ? rpaligned_alloc(alignment, size) : rpmalloc(size);
	if (ptr) {
		size_t usable = rpmalloc_usable_size(ptr);
		m_bytes.fetch_add(usable, std::memory_order_relaxed);
		m_bytesTotal.fetch_add(usable, std::memory_order_relaxed);
		m_allocations.fetch_add(1, std::memory_order_relaxed);
		m_allocationsTotal.fetch_add(1, std::memory_order_relaxed);
	}
	return ptr;
}

void* MemoryAllocator::realloc(void* ptr, size_t newSize, size_t originalAlignment) {
	size_t oldSize = ptr ? rpmalloc_usable_size(ptr) : 0;
	void* newPtr = rpaligned_realloc(ptr, originalAlignment, newSize, oldSize, 0);
	if (newPtr && oldSize) {
		m_bytes.fetch_sub(oldSize, std::memory_order_relaxed);
	}
	if (newPtr) {
		size_t usable = rpmalloc_usable_size(newPtr);
		m_bytes.fetch_add(usable, std::memory_order_relaxed);
		m_bytesTotal.fetch_add(usable - oldSize, std::memory_order_relaxed);
	}
	return newPtr;
}

void MemoryAllocator::free(void*& ptr) {
	if (ptr) {
		m_bytes.fetch_sub(rpmalloc_usable_size(ptr), std::memory_order_relaxed);
		rpfree(ptr);
		ptr = nullptr;
	}
}

// ── Global instances ──

MemoryAllocator g_allocGlobal{"Global"};
MemoryAllocator g_allocRHI{"RHI"};
MemoryAllocator g_allocECS{"ECS"};
MemoryAllocator g_allocRenderer{"Renderer"};
MemoryAllocator g_allocStreaming{"Streaming"};
MemoryAllocator g_allocPhysics{"Physics"};

// ── Virtual memory ──

#ifdef _WIN32
void* virtualReserve(size_t size) { return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS); }
void virtualCommit(void* p, size_t s) { VirtualAlloc(p, s, MEM_COMMIT, PAGE_READWRITE); }
void virtualFree(void* p, size_t r, size_t c) {
	(void)r;
	(void)c;
	VirtualFree(p, 0, MEM_RELEASE);
}
#else
void* virtualReserve(size_t size) {
	void* p = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? nullptr : p;
}
void virtualCommit(void* p, size_t s) { mprotect(p, s, PROT_READ | PROT_WRITE); }
void virtualFree(void* p, size_t r, size_t c) {
	(void)c;
	if (p && r) {
		munmap(p, r);
	}
}
#endif

// ── Write-combined memory ──

void copyToWriteCombined(void* dst, const void* src, size_t bytes) {
	std::memcpy(dst, src, bytes);
}

void copyFromWriteCombined(void* dst, const void* src, size_t bytes) {
	std::memcpy(dst, src, bytes);
}

void writeCombinedBarrier() {
	// No-op on platforms without explicit WC barrier support
}

} // namespace tucano::core
