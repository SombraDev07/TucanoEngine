#include "Core/JobSystem.h"

#include <algorithm>

namespace tucano::core {

JobSystem::JobSystem(uint32_t threadCount) {
  uint32_t n = threadCount;
  if (n == 0) {
    const uint32_t hw = std::max(2u, std::thread::hardware_concurrency());
    n = hw - 1;
  }
  m_workers.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    m_workers.emplace_back([this]() { workerLoop(); });
  }
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_quit = true;
  }
  m_cv.notify_all();
  for (auto& t : m_workers) {
    t.join();
  }
}

void JobSystem::workerLoop() {
  uint64_t seenBatch = 0;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [&]() { return m_quit || m_batchId != seenBatch; });
      if (m_quit) {
        return;
      }
      seenBatch = m_batchId;
    }
    for (;;) {
      const uint32_t idx = m_next.fetch_add(1);
      if (idx >= m_ranges.size()) {
        break;
      }
      (*m_fn)(m_ranges[idx].begin, m_ranges[idx].end);
      if (m_pending.fetch_sub(1) == 1) {
        m_doneCv.notify_all();
      }
    }
  }
}

void JobSystem::parallelFor(uint32_t begin, uint32_t end, uint32_t grain,
                            const std::function<void(uint32_t, uint32_t)>& fn) {
  if (end <= begin) {
    return;
  }
  grain = std::max(1u, grain);
  const uint32_t count = end - begin;
  if (m_workers.empty() || count <= grain) {
    fn(begin, end);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ranges.clear();
    for (uint32_t b = begin; b < end; b += grain) {
      m_ranges.push_back({b, std::min(b + grain, end)});
    }
    m_fn = &fn;
    m_next.store(0);
    m_pending.store(uint32_t(m_ranges.size()));
    ++m_batchId;
  }
  m_cv.notify_all();
  // Caller helps drain the queue.
  for (;;) {
    const uint32_t idx = m_next.fetch_add(1);
    if (idx >= m_ranges.size()) {
      break;
    }
    fn(m_ranges[idx].begin, m_ranges[idx].end);
    if (m_pending.fetch_sub(1) == 1) {
      m_doneCv.notify_all();
    }
  }
  std::unique_lock<std::mutex> lock(m_mutex);
  m_doneCv.wait(lock, [&]() { return m_pending.load() == 0; });
}

} // namespace tucano::core
