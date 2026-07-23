#pragma once

// Background task scheduler for streaming work.
//
// Deliberately separate from JobSystem. They solve opposite problems and sharing one pool for both
// would be a mistake:
//
//   JobSystem      — in-frame data parallelism. parallelFor() BLOCKS the caller until every range
//                    finishes. Right for "split this loop across cores now".
//   TaskScheduler  — background work that must never block a frame. Submit and forget; results are
//                    picked up by a later frame. Right for "read this cell off disk sometime soon".
//
// Streaming needs the second, and JobSystem cannot provide it: it has no queue, only one batch in
// flight, and every entry point waits. Running streaming through it would stall the render thread
// on disk IO.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tucano::core {

/// Lower value runs first. Named rather than numeric so call sites state intent.
enum class TaskPriority : uint8_t {
  Critical = 0, ///< the observer is about to need this; a stall is visible
  High = 1,     ///< inside the load radius
  Normal = 2,   ///< ordinary streaming
  Prefetch = 3, ///< predicted, not yet needed
  Background = 4, ///< cooking, statistics, anything that can wait indefinitely
  Count = 5,
};

/// Handle to one submitted task. Cheap to copy; safe to query after the task finished.
class TaskHandle {
public:
  TaskHandle() = default;

  bool valid() const { return m_state != nullptr; }
  bool finished() const { return m_state && m_state->finished.load(std::memory_order_acquire); }
  bool cancelled() const { return m_state && m_state->cancelled.load(std::memory_order_acquire); }

  /// Requests cancellation. A task already running will finish; one still queued is dropped.
  /// Returns true if it was dropped before running.
  ///
  /// `started` doubles as a claim token: whoever flips it from false to true owns the task. If the
  /// canceller wins the race the worker will find it already claimed and skip the body, so exactly
  /// one of the two acts and there is no window where a "cancelled" task still runs.
  bool cancel() {
    if (!m_state) return false;
    m_state->cancelled.store(true, std::memory_order_release);
    bool expected = false;
    return m_state->started.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
  }

private:
  friend class TaskScheduler;
  struct State {
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancelled{false};
  };
  explicit TaskHandle(std::shared_ptr<State> s) : m_state(std::move(s)) {}
  std::shared_ptr<State> m_state;
};

class TaskScheduler {
public:
  /// `threadCount == 0` picks a conservative default: enough to keep an NVMe queue busy without
  /// starving the render and job threads.
  explicit TaskScheduler(uint32_t threadCount = 0);
  ~TaskScheduler();

  TaskScheduler(const TaskScheduler&) = delete;
  TaskScheduler& operator=(const TaskScheduler&) = delete;

  uint32_t workerCount() const { return uint32_t(m_workers.size()); }

  /// Queues `fn` and returns immediately. Never blocks the caller.
  TaskHandle submit(TaskPriority priority, std::function<void()> fn);

  /// Blocks until every queued and running task has finished. For shutdown and tests, not frames.
  void waitIdle();

  uint32_t pending() const { return m_pending.load(std::memory_order_relaxed); }
  uint32_t running() const { return m_running.load(std::memory_order_relaxed); }
  uint64_t completed() const { return m_completed.load(std::memory_order_relaxed); }

private:
  struct Task {
    TaskPriority priority = TaskPriority::Normal;
    uint64_t sequence = 0; ///< tie-break so equal priorities stay FIFO instead of arbitrary
    std::function<void()> fn;
    std::shared_ptr<TaskHandle::State> state;
  };

  struct TaskOrder {
    bool operator()(const Task& a, const Task& b) const {
      if (a.priority != b.priority) return a.priority > b.priority; // lower enum runs first
      return a.sequence > b.sequence;
    }
  };

  void workerLoop();

  std::vector<std::thread> m_workers;
  std::priority_queue<Task, std::vector<Task>, TaskOrder> m_queue;
  mutable std::mutex m_mutex;
  std::condition_variable m_wake;
  std::condition_variable m_idle;
  std::atomic<uint32_t> m_pending{0};
  std::atomic<uint32_t> m_running{0};
  std::atomic<uint64_t> m_completed{0};
  uint64_t m_sequence = 0;
  bool m_quit = false;
};

} // namespace tucano::core
