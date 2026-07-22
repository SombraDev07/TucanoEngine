#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tucano::core {

// Minimal work-stealing-free thread pool: enqueue + parallelFor (used by ECS MT queries).
class JobSystem {
public:
  explicit JobSystem(uint32_t threadCount = 0); // 0 → hardware_concurrency - 1
  ~JobSystem();

  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  uint32_t workerCount() const { return uint32_t(m_workers.size()); }

  // Runs fn(begin..end) split into ranges across workers + caller; blocks until done.
  void parallelFor(uint32_t begin, uint32_t end, uint32_t grain,
                   const std::function<void(uint32_t, uint32_t)>& fn);

private:
  struct Range {
    uint32_t begin, end;
  };
  void workerLoop();

  std::vector<std::thread> m_workers;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::condition_variable m_doneCv;
  const std::function<void(uint32_t, uint32_t)>* m_fn = nullptr;
  std::vector<Range> m_ranges;
  std::atomic<uint32_t> m_next{0};
  std::atomic<uint32_t> m_pending{0};
  bool m_quit = false;
  uint64_t m_batchId = 0;
};

} // namespace tucano::core
