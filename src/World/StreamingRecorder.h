#pragma once

// WM-10: streaming event recording and replay.
//
// Records every load/unload the scheduler performs, so a session can be inspected after the fact
// ("why did that cell pop in late?") and replayed.
//
// One honest limitation, stated up front because it shapes the whole design: streaming is
// asynchronous and multi-threaded, so the FRAME a given cell finishes loading on is not
// reproducible — it depends on disk timing and thread scheduling. Chasing frame-exact determinism
// from a live run would be a lie.
//
// What IS deterministic, and what this provides:
//   * The recorded log is a faithful account of what actually happened, in order.
//   * Replaying a log is deterministic by construction: you are replaying the recorded decisions,
//     not re-deriving them, so the same log always yields the same resident set.
//   * The FINAL resident set for a given observer path is deterministic even live, because it is a
//     function of position and radius, not of timing.
//
// That is the useful guarantee for a replay system and for a multiplayer host that wants to tell
// clients which cells to activate.

#include "World/CellId.h"
#include "World/WorldGrid.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tucano::world {

enum class StreamingEventType : uint8_t {
  LoadStarted = 0,
  Loaded = 1,
  Unloaded = 2,
  Failed = 3,
};

const char* streamingEventName(StreamingEventType type);

struct StreamingEvent {
  uint64_t frame = 0;
  CellId cell;
  WorldLayer layer = WorldLayer::Gameplay;
  StreamingEventType type = StreamingEventType::LoadStarted;
};

/// Ring-buffered log of streaming events.
///
/// Bounded on purpose: a long session would otherwise grow without limit, and the useful window is
/// the recent past. When it wraps, the oldest events are dropped and `droppedEvents()` says how
/// many — silence about lost history would make the log untrustworthy.
class StreamingRecorder {
public:
  explicit StreamingRecorder(size_t capacity = 10000) : m_capacity(capacity ? capacity : 1) {}

  void record(uint64_t frame, const CellId& cell, WorldLayer layer, StreamingEventType type);

  /// Events oldest-first.
  std::vector<StreamingEvent> events() const;
  size_t eventCount() const;
  uint64_t droppedEvents() const;
  void clear();

  bool save(const std::string& path) const;
  bool load(const std::string& path);

  /// The set of cells left resident by a log: every cell whose last event for a layer was Loaded.
  /// This is the state a replay reproduces, and the thing that is deterministic.
  static std::vector<CellId> residentAfter(const std::vector<StreamingEvent>& log);

  /// True when two logs describe the same outcome — the same cells resident at the end. Frame
  /// numbers and event ordering are deliberately NOT compared: they vary with disk and thread
  /// timing, and demanding they match would fail on a correct system.
  static bool sameOutcome(const std::vector<StreamingEvent>& a,
                          const std::vector<StreamingEvent>& b);

private:
  mutable std::mutex m_mutex;
  std::vector<StreamingEvent> m_ring;
  size_t m_capacity;
  size_t m_head = 0;   ///< next write slot once the ring is full
  bool m_wrapped = false;
  uint64_t m_dropped = 0;
};

} // namespace tucano::world
