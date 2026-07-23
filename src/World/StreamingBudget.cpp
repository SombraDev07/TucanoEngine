#include "World/StreamingBudget.h"

#include <algorithm>
#include <cmath>

namespace tucano::world {
namespace {

/// Cost estimate used before a channel has seen any work. Deliberately small: guessing high would
/// stall streaming on the first frames, and the average corrects within a handful of jobs.
constexpr float kSeedCostMs = 0.25f;

/// Weight of a new sample in the running average. Slow enough to ignore one unlucky job, fast
/// enough to follow a genuine change in cell size.
constexpr float kCostSmoothing = 0.15f;

void atomicAdd(std::atomic<float>& target, float delta) {
  float cur = target.load(std::memory_order_relaxed);
  while (!target.compare_exchange_weak(cur, cur + delta, std::memory_order_relaxed)) {
  }
}

} // namespace

const char* budgetChannelName(BudgetChannel c) {
  switch (c) {
    case BudgetChannel::IO: return "IO";
    case BudgetChannel::CPU: return "CPU";
    case BudgetChannel::GPU: return "GPU";
    default: return "?";
  }
}

// ── BudgetReservation ────────────────────────────────

BudgetReservation::~BudgetReservation() {
  // Never committed: refund the estimate rather than leaving the channel permanently poorer.
  if (m_owner) m_owner->release(m_channel, m_reservedMs, 0.0f, false);
}

BudgetReservation::BudgetReservation(BudgetReservation&& o) noexcept
    : m_owner(o.m_owner), m_channel(o.m_channel), m_reservedMs(o.m_reservedMs) {
  o.m_owner = nullptr;
}

BudgetReservation& BudgetReservation::operator=(BudgetReservation&& o) noexcept {
  if (this != &o) {
    if (m_owner) m_owner->release(m_channel, m_reservedMs, 0.0f, false);
    m_owner = o.m_owner;
    m_channel = o.m_channel;
    m_reservedMs = o.m_reservedMs;
    o.m_owner = nullptr;
  }
  return *this;
}

void BudgetReservation::commit(float actualMs) {
  if (!m_owner) return;
  m_owner->release(m_channel, m_reservedMs, std::max(actualMs, 0.0f), true);
  m_owner = nullptr;
}

// ── StreamingBudget ──────────────────────────────────

void StreamingBudget::beginFrame(float lastFrameMs) {
  ++m_frame;

  // Adapt before clearing, so the decision is made on what actually happened.
  //
  // The signal is headroom against the target frame time. Running fast means there is room to
  // stream harder and get content in sooner; running slow means streaming must yield, because a
  // stutter is worse than a late-arriving bush. The response is deliberately gentle — streaming
  // that reacts sharply to frame-time noise produces visible pop-in waves.
  if (lastFrameMs > 0.0f && m_settings.targetFrameMs > 0.0f) {
    const float headroom = m_settings.targetFrameMs / std::max(lastFrameMs, 0.001f);
    const float target = std::clamp(headroom, m_settings.minScale, m_settings.maxScale);
    m_scale += (target - m_scale) * m_settings.adaptRate;
    m_scale = std::clamp(m_scale, m_settings.minScale, m_settings.maxScale);
  }

  for (auto& used : m_usedMs) used.store(0.0f, std::memory_order_relaxed);
  m_rejections.store(0, std::memory_order_relaxed);
}

float StreamingBudget::totalUsedMs() const {
  float sum = 0.0f;
  for (const auto& used : m_usedMs) sum += used.load(std::memory_order_relaxed);
  return sum;
}

BudgetReservation StreamingBudget::reserve(BudgetChannel channel) {
  float estimate = m_avgCostMs[uint32_t(channel)].load(std::memory_order_relaxed);
  if (estimate <= 0.0f) estimate = kSeedCostMs;
  return reserveExplicit(channel, estimate);
}

BudgetReservation StreamingBudget::reserveExplicit(BudgetChannel channel, float estimateMs) {
  estimateMs = std::max(estimateMs, 0.0f);
  const uint32_t idx = uint32_t(channel);

  const float channelCap = m_settings.channelMs[idx] * m_scale;
  const float totalCap = m_settings.totalMs * m_scale;

  const float channelUsed = m_usedMs[idx].load(std::memory_order_relaxed);
  const float totalUsed = totalUsedMs();

  // One exception to both caps: on a frame where nothing has been spent at all, let a single job
  // through however expensive it is. Otherwise a cell costing more than the entire budget would be
  // refused every frame for ever and never load. The escape hatch is keyed on the whole frame, not
  // on the channel — keying it per channel would let an untouched channel walk straight past the
  // combined ceiling.
  const bool frameUntouched = totalUsed <= 0.0f;

  if (!frameUntouched && channelUsed + estimateMs > channelCap) {
    m_rejections.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  if (!frameUntouched && totalUsed + estimateMs > totalCap) {
    m_rejections.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  atomicAdd(m_usedMs[idx], estimateMs);
  return BudgetReservation(this, channel, estimateMs);
}

void StreamingBudget::release(BudgetChannel channel, float reservedMs, float actualMs,
                              bool commit) {
  const uint32_t idx = uint32_t(channel);

  // Swap the estimate for the truth. If the job overran, the channel stays charged for the
  // difference and the next reservation this frame sees it — which is the whole point of
  // reserving up front.
  atomicAdd(m_usedMs[idx], (commit ? actualMs : 0.0f) - reservedMs);

  if (commit) {
    float avg = m_avgCostMs[idx].load(std::memory_order_relaxed);
    const float updated = avg <= 0.0f ? actualMs : avg + (actualMs - avg) * kCostSmoothing;
    m_avgCostMs[idx].store(updated, std::memory_order_relaxed);
  }
}

} // namespace tucano::world
