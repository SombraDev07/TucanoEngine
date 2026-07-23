#include "Core/TaskScheduler.h"

#include <algorithm>

namespace tucano::core {

TaskScheduler::TaskScheduler(uint32_t threadCount) {
  if (threadCount == 0) {
    // Streaming is latency-bound on IO, not throughput-bound on ALU, so a small pool keeps an
    // NVMe queue saturated without competing with the render thread or JobSystem's workers.
    const uint32_t hw = std::max(2u, std::thread::hardware_concurrency());
    threadCount = std::clamp(hw / 4u, 2u, 8u);
  }

  m_workers.reserve(threadCount);
  for (uint32_t i = 0; i < threadCount; ++i) {
    m_workers.emplace_back([this] { workerLoop(); });
  }
}

TaskScheduler::~TaskScheduler() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_quit = true;
  }
  m_wake.notify_all();
  for (auto& t : m_workers) {
    if (t.joinable()) t.join();
  }
}

TaskHandle TaskScheduler::submit(TaskPriority priority, std::function<void()> fn) {
  auto state = std::make_shared<TaskHandle::State>();

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_quit) {
      // Shutting down: report the task as finished rather than leaving a caller waiting forever.
      state->finished.store(true, std::memory_order_release);
      return TaskHandle(std::move(state));
    }
    m_queue.push(Task{priority, m_sequence++, std::move(fn), state});
    m_pending.fetch_add(1, std::memory_order_relaxed);
  }
  m_wake.notify_one();
  return TaskHandle(std::move(state));
}

void TaskScheduler::waitIdle() {
  std::unique_lock<std::mutex> lock(m_mutex);
  m_idle.wait(lock, [this] {
    return m_queue.empty() && m_running.load(std::memory_order_relaxed) == 0;
  });
}

void TaskScheduler::workerLoop() {
  for (;;) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_wake.wait(lock, [this] { return m_quit || !m_queue.empty(); });
      if (m_quit && m_queue.empty()) return;

      // priority_queue only exposes a const top(), and std::function is move-only in practice, so
      // const_cast to steal it. Safe: the element is popped immediately after.
      task = std::move(const_cast<Task&>(m_queue.top()));
      m_queue.pop();
      m_pending.fetch_sub(1, std::memory_order_relaxed);
      m_running.fetch_add(1, std::memory_order_relaxed);
    }

    // Claim it. If cancel() already flipped this, the task was dropped and must not run.
    bool expected = false;
    const bool claimed =
        task.state->started.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    if (claimed) {
      // A throwing task must not take a worker thread down with it — that would silently shrink
      // the pool and eventually deadlock waitIdle().
      try {
        task.fn();
      } catch (...) {
        // Swallowed on purpose: the handle reports finished, and the streaming layer above decides
        // what a failed cell means. Rethrowing here has nobody to catch it.
      }
    }
    task.state->finished.store(true, std::memory_order_release);

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_running.fetch_sub(1, std::memory_order_relaxed);
      m_completed.fetch_add(1, std::memory_order_relaxed);
      if (m_queue.empty() && m_running.load(std::memory_order_relaxed) == 0) {
        m_idle.notify_all();
      }
    }
  }
}

} // namespace tucano::core
