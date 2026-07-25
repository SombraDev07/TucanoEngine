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
#include "World/StreamingRecorder.h"
#include "World/StreamingTypes.h"
#include "World/WorldGrid.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

  /// Layer composition (WM-7). Each layer gets its own slice of the observer's load radius, as a
  /// fraction of it. Gameplay covers the whole radius so colliders and triggers exist well before
  /// the player arrives; Detail covers a small fraction so decals and small vegetation only exist
  /// where they can actually be seen. That difference is the point of layers — without it every
  /// layer loads at the same distance and "composition" is just a label.
  float layerRadiusScale[kLayerCount] = {1.0f, 0.85f, 0.6f, 0.4f};

  /// Per-layer kill switch, e.g. a dedicated server that wants Gameplay and nothing else.
  bool layerEnabled[kLayerCount] = {true, true, true, true};

  /// HLOD (WM-5) distance bands. A cell closer than lodDistance[0] loads at full detail; beyond
  /// each threshold it loads one level coarser. Empty disables HLOD entirely.
  ///
  /// Crossing a band RELOADS the cell at the new level. That is the honest cost of switching
  /// representation — there is no way to turn dozens of objects into one merged mesh in place —
  /// and it is why the bands are far apart: a boundary you cross often would thrash.
  std::vector<float> lodDistances = {160.0f, 400.0f};
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

  /// Enables event recording (WM-10). Null (the default) disables it at zero cost. The recorder is
  /// owned by the caller and must outlive the scheduler.
  void setRecorder(StreamingRecorder* recorder) { m_recorder = recorder; }
  StreamingRecorder* recorder() const { return m_recorder; }

  /// Replaces the observer set. Cheap; call it every frame with the current cameras/players.
  void setObservers(const std::vector<StreamingObserver>& observers);

  /// Hot reload (WM-9): forces a resident cell to be torn down and re-streamed from disk on the next
  /// update, keeping its place in the world. This is how an edit in the editor becomes visible in a
  /// running game without reloading the world: the file watcher calls this when a `.tcell` changes.
  /// Reuses the same release+reload path a LOD-band crossing uses. A cell that is not currently
  /// resident is simply reloaded when it next comes into range. MUST be called on the update thread.
  void requestReload(const CellId& id);

  /// Advances streaming by one frame. MUST be called on the thread that owns the graphics device:
  /// it drains the GPU-upload stage inline. `lastFrameMs` feeds the adaptive budget.
  void update(float lastFrameMs);

  /// Blocks until every in-flight load has drained. For shutdown and tests. Because stage 3 runs
  /// in update(), this pumps update() itself until the pipeline is empty.
  void drain();

  const StreamingStats& stats() const { return m_stats; }
  const StreamingSchedulerDesc& desc() const { return m_desc; }
  StreamingSchedulerDesc& desc() { return m_desc; }

  /// One resident (or loading) cell, for the editor's World Outliner. A snapshot, not a live view.
  struct ResidentCellInfo {
    CellId id;
    uint32_t layerMask = 0; ///< bit l set when layer l is loaded
    uint32_t loadingMask = 0; ///< bit l set when layer l is mid-pipeline
    uint32_t lod = 0;       ///< coarsest loaded LOD across the cell's layers
    float distance = 0.0f;  ///< box distance to the nearest active observer
  };

  /// Snapshots every tracked cell that has at least one layer loaded or loading, nearest first.
  /// Call between update()s on the same thread — it reads the tracked map without locking.
  void snapshotResident(std::vector<ResidentCellInfo>& out) const;

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
    /// Detail level each layer was actually loaded at, so a change can be detected.
    std::array<uint32_t, kLayerCount> loadedLod{};
  };

  /// Detail level for a cell, from its distance to the nearest observer.
  uint32_t lodFor(const WorldCell& cell) const;

  void gatherWantedCells(std::vector<std::pair<CellId, float>>& wanted);
  void addPrefetchCells(std::unordered_map<uint64_t, float>& merged);
  /// True when some active observer is close enough for THIS layer specifically. `forUnload`
  /// picks the observer's unload radius instead of its load radius — the gap between the two is
  /// the per-layer hysteresis, mirroring the cell-level one.
  bool layerWanted(const WorldCell& cell, WorldLayer layer, bool forUnload) const;
  float priorityOf(const WorldCell& cell, WorldLayer layer);
  void startLayerLoad(TrackedCell& tracked, WorldLayer layer, float priority, bool prefetch,
                      uint32_t lod);
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

  mutable std::mutex m_observerMutex;
  std::vector<StreamingObserver> m_observers;

  /// Tracked cells, keyed by cell id. Only touched from update()/drain() on the main thread, so it
  /// needs no lock; the background stages touch only their own LayerLoad, never this map.
  std::unordered_map<uint64_t, TrackedCell> m_tracked;

  CellPersistenceStore* m_persistence = nullptr;
  StreamingRecorder* m_recorder = nullptr;
  std::unordered_map<uint32_t, MovementPredictor> m_predictors; ///< keyed by observer id

  /// Cells whose disk content changed and must be re-streamed in place (WM-9). Consumed in update();
  /// a cell stays requested until it is next wanted, so an edit to an out-of-range cell still takes
  /// effect when the player returns.
  std::unordered_set<uint64_t> m_reloadRequests;

  std::atomic<uint32_t> m_inFlight{0};
  uint64_t m_frame = 0;
  StreamingStats m_stats;
};

} // namespace tucano::world
