#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// FASE 1: Custom memory system with rpmalloc + per-thread heaps + named allocators.
// Based on Esoterica's Code/Base/Memory/Memory.h, adapted for Tucano.

namespace tucano::core {

// ── Initialization ──

void memoryInit();
void memoryShutdown();
void memoryInitThreadHeap();
bool memoryHasThreadHeap();

// ── Named allocator with thread-safe statistics ──

class MemoryAllocator {
public:
	explicit MemoryAllocator(const char* name);
	~MemoryAllocator() = default;

	const char* name() const { return m_name; }

	void* alloc(size_t size, size_t alignment = 16);
	void* realloc(void* ptr, size_t newSize, size_t originalAlignment = 16);
	void  free(void*& ptr);

	uint64_t bytesAllocated() const    { return m_bytes.load(); }
	uint64_t allocationCount() const   { return m_allocations.load(); }
	uint64_t totalBytes() const        { return m_bytesTotal.load(); }
	uint64_t totalAllocations() const  { return m_allocationsTotal.load(); }

private:
	std::atomic<uint64_t> m_bytes{0};
	std::atomic<uint64_t> m_allocations{0};
	std::atomic<uint64_t> m_bytesTotal{0};
	std::atomic<uint64_t> m_allocationsTotal{0};
	const char* m_name = nullptr;
};

// ── Global allocator instances ──

extern MemoryAllocator g_allocGlobal;    // default new/delete
extern MemoryAllocator g_allocRHI;       // GPU resources, descriptors
extern MemoryAllocator g_allocECS;       // entities, components, archetypes
extern MemoryAllocator g_allocRenderer;  // renderer internal
extern MemoryAllocator g_allocStreaming; // world streaming, IO buffers
extern MemoryAllocator g_allocPhysics;   // Jolt bodies, constraints

// ── Per-allocator new/delete helpers ──

template<typename T, typename... Args>
[[nodiscard]] T* allocNew(MemoryAllocator& alloc, Args&&... args) {
	void* p = alloc.alloc(sizeof(T), alignof(T));
	return new(p) T(std::forward<Args>(args)...);
}

template<typename T>
void allocDelete(MemoryAllocator& alloc, T*& ptr) {
	if (ptr) {
		ptr->~T();
		alloc.free(reinterpret_cast<void*&>(ptr));
	}
}

// ── Virtual memory ──

void* virtualReserve(size_t size);
void  virtualCommit(void* ptr, size_t size);
void  virtualFree(void* ptr, size_t reserved, size_t committed);

// ── Write-combined memory ──

void copyToWriteCombined(void* dst, const void* src, size_t bytes);
void copyFromWriteCombined(void* dst, const void* src, size_t bytes);
void writeCombinedBarrier();

} // namespace tucano::core
