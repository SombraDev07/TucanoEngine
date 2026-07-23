#pragma once

// WM-2: the streaming scheduler.
//
// It watches a set of observers, decides which cells should be resident, and moves them through a
// three-stage load pipeline (disk → CPU → GPU) without ever blocking the frame. Unload runs on an
// LRU with hysteresis so cells do not thrash at a radius boundary.
//
// The design points that matter:
//
//   * Stages 1 and 2 run on the TaskScheduler (background threads); stage 3 runs on the calling
//     thread because the RHI is single-threaded. See CellDataProvider for the threading contract.
//   * Every stage takes a budget reservation before it starts, so a heavy frame throttles work
//     instead of blowing the frame time.
//   * A cell that leaves every load radius while still loading is cancelled mid-pipeline rather
//     than wastefully finished — the player already ran past it.
//   * Priority is distance scaled by layer weight and observer bias, recomputed each frame, so the
//     nearest gameplay layer of the nearest cell always wins.

#include "Core/TaskScheduler.h"
#include "World/CellPersistence.h"
#include "World/MovementPredictor.h"
#include "World/StreamingBudget.h"
#include "World/StreamingTypes.h"
#include "World/WorldGrid.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace tucano::world {

struct StreamingSchedulerDesc {
  /// Level the streamer operates on. Cells at this level are the unit of load/unload.
  uint32_t streamLevel = 10;
  /// Most cells allowed mid-pipeline at once. Caps memory spikes and disk queue depth.
  uint32_t maxConcurrentLoads = 32;
  /// A layer must sit unwanted for this many frames before it is unloaded, on top of the radius
  /// hysteresis. Stops a one-frame radius flicker from tearing a cell down.
  uint64_t unloadGraceFrames = 120;

  /// Movement prediction (WM-3). Prefetched cells load at low priority ahead of the observer.
  PredictionSettings prediction;
};

class StreamingScheduler {
public:
  StreamingScheduler(WorldGrid& grid, core::TaskScheduler& tasks, StreamingBudget& budget,
                     CellDataProvider& provider, const StreamingSchedulerDesc& desc = {});
  ~StreamingScheduler();

  StreamingScheduler(const StreamingScheduler&) = delete;
  StreamingScheduler& operator=(const StreamingScheduler&) = delete;

  /// Enables cell persistence (WM-2.5). When set, runtime mutations are captured on unload and
  /// replayed on reload via the provider's captureDelta/applyDelta. Null (the default) disables it
  /// with zero overhead. The store is owned by the caller and outlives the scheduler.
  void setPersistence(CellPersistenceStore* store) { m_persistence = store; }
  CellPersistenceStore* persistence() const { return m_persistence; }

  /// Replaces the observer set. Cheap; call it every frame with the current cameras/players.
  void setObservers(const std::vector<StreamingObserver>& observers);

  /// Advances streaming by one frame. MUST be called on the thread that owns the graphics device:
  /// it drains the GPU-upload stage inline. `lastFrameMs` feeds the adaptive budget.
  void update(float lastFrameMs);

  /// Blocks until every in-flight load has drained. For shutdown and tests. Because stage 3 runs
  /// in update(), this pumps update() itself until the pipeline is empty.
  void drain();

  const StreamingStats& stats() const { return m_stats; }
  const StreamingSchedulerDesc& desc() const { return m_desc; }
  StreamingSchedulerDesc& desc() { return m_desc; }

private:
  /// Per-layer progress through the pipeline. One of these exists per (cell, layer) that is being
  /// or has been loaded.
  struct LayerLoad {
    CellId cell;
    WorldLayer layer = WorldLayer::Gameplay;
    float priority = 1e30f;

    core::TaskHandle task;              ///< the background stage currently running, if any
    std::vector<uint8_t> bytes;        ///< stage 1 output, consumed by stage 2
    CellContent content;               ///< stage 2/3 output
    std::atomic<CellState> stage{CellState::Queued};
    std::atomic<bool> ioDone{false};   ///< stage 1 finished, ready for stage 2 to be scheduled
    std::atomic<bool> cpuDone{false};  ///< stage 2 finished, ready for stage 3 (main thread)
    std::atomic<bool> failed{false};

    /// Set when the cell leaves every radius mid-flight. The next safe point tears it down.
    std::atomic<bool> abandoned{false};
  };

  /// Everything the scheduler tracks for one resident-or-loading cell.
  struct TrackedCell {
    CellId id;
    uint64_t lastWantedFrame = 0;
    std::array<std::unique_ptr<LayerLoad>, kLayerCount> loads;
    std::array<bool, kLayerCount> loaded{};
  };

  void gatherWantedCells(std::vector<std::pair<CellId, float>>& wanted);
  void addPrefetchCells(std::unordered_map<uint64_t, float>& merged);
  float priorityOf(const WorldCell& cell, WorldLayer layer);
  void startLayerLoad(TrackedCell& tracked, WorldLayer layer, float priority, bool prefetch);
  void pumpIoToCpu(LayerLoad& load);
  void pumpCpuToGpu(LayerLoad& load);
  void finishLayer(TrackedCell& tracked, WorldLayer layer, bool ok);
  void considerUnloads();
  void releaseLayer(const CellId& id, WorldLayer layer, LayerLoad& load);

  WorldGrid& m_grid;
  core::TaskScheduler& m_tasks;
  StreamingBudget& m_budget;
  CellDataProvider& m_provider;
  StreamingSchedulerDesc m_desc;

  std::mutex m_observerMutex;
  std::vector<StreamingObserver> m_observers;

  /// Tracked cells, keyed by cell id. Only touched from update()/drain() on the main thread, so it
  /// needs no lock; the background stages touch only their own LayerLoad, never this map.
  std::unordered_map<uint64_t, TrackedCell> m_tracked;

  CellPersistenceStore* m_persistence = nullptr;
  std::unordered_map<uint32_t, MovementPredictor> m_predictors; ///< keyed by observer id
  std::atomic<uint32_t> m_inFlight{0};
  uint64_t m_frame = 0;
  StreamingStats m_stats;
};

} // namespace tucano::world
