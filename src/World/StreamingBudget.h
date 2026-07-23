#pragma once

// Per-frame spending limits for streaming work.
//
// One correction to the roadmap's design. It has spendIO/spendCPU/spendGPU called with the time a
// job took — but a job's cost is only known once it has finished, and by then the budget has
// already been blown. Charging after the fact turns the budget into a report, not a limit.
//
// So this reserves before the work starts, using an estimate, and reconciles after with the real
// number. The estimate is not guessed: each channel keeps a running average of what its jobs
// actually cost, so a world of heavy cells self-corrects within a few frames without anyone
// tuning a constant.

#include <atomic>
#include <cstdint>

namespace tucano::world {

/// The three resources streaming competes for. They are tracked separately because they are
/// genuinely independent: a frame can be disk-bound while the GPU copy queue sits idle.
enum class BudgetChannel : uint8_t {
  IO = 0,  ///< disk read
  CPU = 1, ///< deserialize, entity creation
  GPU = 2, ///< buffer and texture upload
  Count = 3,
};

inline constexpr uint32_t kBudgetChannels = uint32_t(BudgetChannel::Count);

const char* budgetChannelName(BudgetChannel c);

struct StreamingBudgetSettings {
  float channelMs[kBudgetChannels] = {2.0f, 3.0f, 1.0f};
  /// Ceiling across all channels combined. Lower than the sum on purpose: the channels overlap in
  /// wall-clock time, so allowing every one to hit its individual cap would overshoot the frame.
  float totalMs = 5.0f;

  /// Frame time the game is aiming for. Budgets shrink when the frame is already over it.
  float targetFrameMs = 16.6f;
  /// How fast the adaptive budget reacts. Low so streaming does not oscillate with frame noise.
  float adaptRate = 0.05f;
  /// Bounds on the adaptive scale, so a bad frame cannot stop streaming entirely.
  float minScale = 0.25f;
  float maxScale = 2.0f;
};

/// A reservation taken out against a channel. Release it with `commit` once the real cost is
/// known; letting it go out of scope without committing refunds the estimate.
class BudgetReservation {
public:
  BudgetReservation() = default;
  ~BudgetReservation();

  BudgetReservation(const BudgetReservation&) = delete;
  BudgetReservation& operator=(const BudgetReservation&) = delete;
  BudgetReservation(BudgetReservation&& o) noexcept;
  BudgetReservation& operator=(BudgetReservation&& o) noexcept;

  bool valid() const { return m_owner != nullptr; }
  explicit operator bool() const { return valid(); }

  float reservedMs() const { return m_reservedMs; }

  /// Replaces the estimate with what the work really cost and feeds the channel's average.
  void commit(float actualMs);

private:
  friend class StreamingBudget;
  BudgetReservation(class StreamingBudget* owner, BudgetChannel channel, float ms)
      : m_owner(owner), m_channel(channel), m_reservedMs(ms) {}

  class StreamingBudget* m_owner = nullptr;
  BudgetChannel m_channel = BudgetChannel::IO;
  float m_reservedMs = 0.0f;
};

class StreamingBudget {
public:
  explicit StreamingBudget(const StreamingBudgetSettings& s = {}) : m_settings(s) {}

  StreamingBudgetSettings& settings() { return m_settings; }
  const StreamingBudgetSettings& settings() const { return m_settings; }

  /// Clears per-frame spending and adapts the scale to how the last frame went.
  /// `lastFrameMs` is the real frame time, including everything streaming did.
  void beginFrame(float lastFrameMs);

  /// Tries to reserve room for one job. Returns an invalid reservation when the channel or the
  /// combined budget is already spent, which the scheduler reads as "not this frame".
  BudgetReservation reserve(BudgetChannel channel);

  /// Reserve with a caller-supplied estimate instead of the learned average. Use when the size is
  /// known up front — a 40 MB cell should not be charged the average cell's cost.
  BudgetReservation reserveExplicit(BudgetChannel channel, float estimateMs);

  float usedMs(BudgetChannel c) const {
    return m_usedMs[uint32_t(c)].load(std::memory_order_relaxed);
  }
  float budgetMs(BudgetChannel c) const {
    return m_settings.channelMs[uint32_t(c)] * m_scale;
  }
  float totalUsedMs() const;
  float scale() const { return m_scale; }

  /// Running average cost of a job on this channel, in milliseconds.
  float averageCostMs(BudgetChannel c) const {
    return m_avgCostMs[uint32_t(c)].load(std::memory_order_relaxed);
  }

  uint64_t frameIndex() const { return m_frame; }

  /// Jobs refused this frame because the budget was exhausted. A steadily rising number means the
  /// budget is too tight for the content, not that streaming is broken.
  uint32_t rejections() const { return m_rejections.load(std::memory_order_relaxed); }

private:
  friend class BudgetReservation;
  void release(BudgetChannel channel, float reservedMs, float actualMs, bool commit);

  StreamingBudgetSettings m_settings;
  std::atomic<float> m_usedMs[kBudgetChannels] = {};
  std::atomic<float> m_avgCostMs[kBudgetChannels] = {};
  std::atomic<uint32_t> m_rejections{0};
  float m_scale = 1.0f;
  uint64_t m_frame = 0;
};

} // namespace tucano::world
