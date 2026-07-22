#pragma once

#include "RHI/RHI.h"

#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::rhi {

// Disk-backed PSO cache + async compile drain (Dagor ASYNC_PIPELINE_COMPILE).
class PipelineCache {
public:
  explicit PipelineCache(const std::string& cachePath);

  bool load();
  bool save() const;

  std::vector<uint8_t> find(uint64_t key) const;
  void store(uint64_t key, const void* data, size_t size);

  static uint64_t hashBytes(const void* data, size_t size, uint64_t seed = 14695981039346656037ull);
  static uint64_t hashCombine(uint64_t a, uint64_t b);

  uint32_t hits() const { return m_hits; }
  uint32_t misses() const { return m_misses; }
  size_t entryCount() const { return m_entries.size(); }

  // Queue a background store of a compiled blob (non-blocking).
  void storeAsync(uint64_t key, std::vector<uint8_t> blob);
  void pumpAsync();
  uint32_t asyncPending() const;

private:
  std::string m_path;
  std::unordered_map<uint64_t, std::vector<uint8_t>> m_entries;
  mutable uint32_t m_hits = 0;
  mutable uint32_t m_misses = 0;
  mutable std::mutex m_mutex;

  struct AsyncJob {
    uint64_t key = 0;
    std::future<std::vector<uint8_t>> future;
  };
  std::vector<AsyncJob> m_async;
  mutable std::mutex m_asyncMutex;
};

} // namespace tucano::rhi
