#include "RHI/DX12/PipelineCache.h"

#include <chrono>
#include <fstream>

namespace tucano::rhi {

PipelineCache::PipelineCache(const std::string& cachePath) : m_path(cachePath) {}

uint64_t PipelineCache::hashBytes(const void* data, size_t size, uint64_t seed) {
  auto h = seed;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

uint64_t PipelineCache::hashCombine(uint64_t a, uint64_t b) {
  return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
}

bool PipelineCache::load() {
  std::lock_guard lock(m_mutex);
  m_entries.clear();
  std::ifstream in(m_path, std::ios::binary);
  if (!in) {
    return false;
  }
  uint32_t magic = 0, version = 0, count = 0;
  in.read(reinterpret_cast<char*>(&magic), 4);
  in.read(reinterpret_cast<char*>(&version), 4);
  in.read(reinterpret_cast<char*>(&count), 4);
  if (magic != 0x54435043u || version != 1) { // 'TCPC'
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t key = 0;
    uint32_t size = 0;
    in.read(reinterpret_cast<char*>(&key), 8);
    in.read(reinterpret_cast<char*>(&size), 4);
    std::vector<uint8_t> blob(size);
    if (size) {
      in.read(reinterpret_cast<char*>(blob.data()), size);
    }
    m_entries.emplace(key, std::move(blob));
  }
  return true;
}

bool PipelineCache::save() const {
  std::lock_guard lock(m_mutex);
  std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  const uint32_t magic = 0x54435043u;
  const uint32_t version = 1;
  const uint32_t count = static_cast<uint32_t>(m_entries.size());
  out.write(reinterpret_cast<const char*>(&magic), 4);
  out.write(reinterpret_cast<const char*>(&version), 4);
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (const auto& [key, blob] : m_entries) {
    const uint32_t size = static_cast<uint32_t>(blob.size());
    out.write(reinterpret_cast<const char*>(&key), 8);
    out.write(reinterpret_cast<const char*>(&size), 4);
    if (size) {
      out.write(reinterpret_cast<const char*>(blob.data()), size);
    }
  }
  return true;
}

std::vector<uint8_t> PipelineCache::find(uint64_t key) const {
  std::lock_guard lock(m_mutex);
  auto it = m_entries.find(key);
  if (it == m_entries.end()) {
    ++m_misses;
    return {};
  }
  ++m_hits;
  return it->second;
}

void PipelineCache::store(uint64_t key, const void* data, size_t size) {
  std::lock_guard lock(m_mutex);
  m_entries[key] = std::vector<uint8_t>(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
}

void PipelineCache::storeAsync(uint64_t key, std::vector<uint8_t> blob) {
  std::lock_guard lock(m_asyncMutex);
  AsyncJob job;
  job.key = key;
  job.future = std::async(std::launch::async, [b = std::move(blob)]() mutable { return std::move(b); });
  m_async.push_back(std::move(job));
}

void PipelineCache::pumpAsync() {
  std::lock_guard lock(m_asyncMutex);
  std::vector<AsyncJob> keep;
  for (auto& job : m_async) {
    if (job.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      auto blob = job.future.get();
      store(job.key, blob.data(), blob.size());
    } else {
      keep.push_back(std::move(job));
    }
  }
  m_async = std::move(keep);
}

uint32_t PipelineCache::asyncPending() const {
  std::lock_guard lock(m_asyncMutex);
  return static_cast<uint32_t>(m_async.size());
}

} // namespace tucano::rhi
