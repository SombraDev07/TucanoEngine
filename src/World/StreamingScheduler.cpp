#include "World/StreamingScheduler.h"

#include <algorithm>
#include <chrono>

namespace tucano::world {
namespace {

float nowMs() {
  using namespace std::chrono;
  return duration<float, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/// Priority offset that pushes every prefetch cell behind every genuinely-wanted cell. Real load
/// priorities are distance/weight — at most a few thousand for any sane radius — so a millionfold
/// penalty guarantees prefetch only ever consumes capacity the real load set left unused.
constexpr float kPrefetchPenalty = 1.0e6f;

} // namespace

StreamingScheduler::StreamingScheduler(WorldGrid& grid, core::TaskScheduler& tasks,
                                       StreamingBudget& budget, CellDataProvider& provider,
                                       const StreamingSchedulerDesc& desc)
    : m_grid(grid), m_tasks(tasks), m_budget(budget), m_provider(provider), m_desc(desc) {}

StreamingScheduler::~StreamingScheduler() {
  // Nothing may still be reading into a LayerLoad we are about to free. The background stages hold
  // raw pointers into m_tracked's LayerLoads, so wait them out before the map is destroyed.
  for (auto& [key, tracked] : m_tracked) {
    for (auto& load : tracked.loads) {
      if (load && load->task.valid()) {
        load->abandoned.store(true, std::memory_order_release);
        while (!load->task.finished()) {
          std::this_thread::yield();
        }
      }
    }
  }
}

void StreamingScheduler::setObservers(const std::vector<StreamingObserver>& observers) {
  std::lock_guard<std::mutex> lock(m_observerMutex);
  m_observers = observers;
}

float StreamingScheduler::priorityOf(const WorldCell& cell, WorldLayer layer) {
  // Lower is more urgent. Distance is the base; the layer weight and observer bias scale it so the
  // nearest gameplay layer beats a nearer visual layer of a further cell.
  std::lock_guard<std::mutex> lock(m_observerMutex);
  float best = 1e30f;
  for (const auto& obs : m_observers) {
    if (!obs.active) continue;
    const float dist = cell.distanceTo(obs.position);
    if (dist > obs.loadRadius) continue;
    // Dividing by the layer weight makes a heavier layer (gameplay = 2.0) sort earlier.
    const float p = dist * obs.priorityBias / kLayerWeight[uint32_t(layer)];
    best = std::min(best, p);
  }
  return best;
}

void StreamingScheduler::gatherWantedCells(std::vector<std::pair<CellId, float>>& wanted) {
  wanted.clear();
  std::vector<StreamingObserver> observers;
  {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    observers = m_observers;
  }

  // Union of every observer's load sphere, deduped by cell key. A cell wanted by two observers
  // takes the more urgent (smaller) priority.
  std::unordered_map<uint64_t, float> merged;
  for (const auto& obs : observers) {
    if (!obs.active) continue;
    const auto cells = m_grid.cellsInRadius(obs.position, obs.loadRadius, m_desc.streamLevel);
    for (const CellId& id : cells) {
      WorldCell& cell = m_grid.getOrCreate(id);
      cell.lastTouchFrame.store(m_frame, std::memory_order_relaxed);
      // Priority for the cell as a whole uses the gameplay weight; per-layer priority is refined
      // when the layer is actually scheduled.
      const float dist = cell.distanceTo(obs.position);
      const float p = dist * obs.priorityBias / kLayerWeight[uint32_t(WorldLayer::Gameplay)];
      auto it = merged.find(id.key());
      if (it == merged.end() || p < it->second) merged[id.key()] = p;
    }
  }

  // Prediction adds cells ahead of each observer at a heavy priority penalty, so they only load on
  // spare capacity and never delay a cell the observer can already see.
  addPrefetchCells(merged);

  wanted.reserve(merged.size());
  for (const auto& [key, p] : merged) wanted.emplace_back(CellId::fromKey(key), p);
  // Most urgent first, so the concurrency cap spends its slots on the closest cells.
  std::sort(wanted.begin(), wanted.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
}

void StreamingScheduler::addPrefetchCells(std::unordered_map<uint64_t, float>& merged) {
  if (!m_desc.prediction.enabled) return;

  std::vector<glm::vec3> points;
  for (const auto& [id, predictor] : m_predictors) {
    if (!predictor.moving(m_desc.prediction)) continue;
    predictor.samplePoints(m_desc.prediction, points);

    for (const glm::vec3& p : points) {
      const auto cells = m_grid.cellsInRadius(p, m_desc.prediction.prefetchRadius, m_desc.streamLevel);
      for (const CellId& cid : cells) {
        WorldCell& cell = m_grid.getOrCreate(cid);
        cell.lastTouchFrame.store(m_frame, std::memory_order_relaxed);
        // Rank prefetch cells among themselves by how close the predicted point is, but keep them
        // all far behind any real load via the penalty.
        const float p2 = kPrefetchPenalty + cell.distanceTo(p);
        auto it = merged.find(cid.key());
        // Never downgrade a cell the real radius already wants — take the more urgent priority.
        if (it == merged.end()) merged[cid.key()] = p2;
        else it->second = std::min(it->second, p2);
      }
    }
  }
}

void StreamingScheduler::startLayerLoad(TrackedCell& tracked, WorldLayer layer, float priority,
                                        bool prefetch) {
  auto& slot = tracked.loads[uint32_t(layer)];
  if (slot) return; // already loading or loaded
  if (m_inFlight.load(std::memory_order_relaxed) >= m_desc.maxConcurrentLoads) return;

  // Stage 1 needs an IO reservation. If the disk budget is spent this frame, try again next.
  auto reservation = std::make_shared<BudgetReservation>(m_budget.reserve(BudgetChannel::IO));
  if (!reservation->valid()) return;

  auto load = std::make_unique<LayerLoad>();
  load->cell = tracked.id;
  load->layer = layer;
  load->priority = priority;
  load->stage.store(CellState::LoadingIO, std::memory_order_release);

  WorldCell& cell = m_grid.getOrCreate(tracked.id);
  cell.layers[uint32_t(layer)].state.store(CellState::LoadingIO, std::memory_order_release);

  LayerLoad* raw = load.get();
  m_inFlight.fetch_add(1, std::memory_order_relaxed);
  ++m_stats.loadsStarted;

  // Stage 1 on a worker: read bytes off disk. Records the real cost against the reservation so the
  // IO channel learns this world's actual read time.
  const CellId id = tracked.id;
  CellDataProvider& provider = m_provider;
  // Prefetch reads run at a lower task priority so a real load queued at the same moment jumps
  // ahead of them in the worker pool, not just in the wanted-cell sort.
  const core::TaskPriority taskPrio =
      prefetch ? core::TaskPriority::Prefetch : core::TaskPriority::Normal;
  load->task = m_tasks.submit(taskPrio, [raw, id, layer, provider = &provider, reservation] {
    if (raw->abandoned.load(std::memory_order_acquire)) return;
    const float t0 = nowMs();
    const bool ok = provider->readBytes(id, layer, raw->bytes);
    reservation->commit(nowMs() - t0);
    if (!ok) {
      // No data for this layer is not a failure — an empty layer just finishes immediately. A
      // false from a layer that should exist is caught by deserialize returning false later; here
      // we treat "no bytes" as an empty layer.
      raw->bytes.clear();
    }
    raw->ioDone.store(true, std::memory_order_release);
  });

  slot = std::move(load);
}

void StreamingScheduler::pumpIoToCpu(LayerLoad& load) {
  if (!load.ioDone.load(std::memory_order_acquire)) return;
  if (load.stage.load(std::memory_order_acquire) != CellState::LoadingIO) return;

  if (load.abandoned.load(std::memory_order_acquire)) return;

  // Empty layer: nothing to deserialize, jump straight to done.
  if (load.bytes.empty()) {
    load.cpuDone.store(true, std::memory_order_release);
    load.stage.store(CellState::LoadingGPU, std::memory_order_release);
    return;
  }

  auto reservation = std::make_shared<BudgetReservation>(m_budget.reserve(BudgetChannel::CPU));
  if (!reservation->valid()) return; // retry next frame; IO output is kept

  load.stage.store(CellState::LoadingCPU, std::memory_order_release);
  WorldCell* cell = m_grid.find(load.cell);
  if (cell) {
    cell->layers[uint32_t(load.layer)].state.store(CellState::LoadingCPU, std::memory_order_release);
  }

  LayerLoad* raw = &load;
  const CellId id = load.cell;
  const WorldLayer layer = load.layer;
  CellDataProvider* provider = &m_provider;
  load.task = m_tasks.submit(core::TaskPriority::Normal, [raw, id, layer, provider, reservation] {
    if (raw->abandoned.load(std::memory_order_acquire)) return;
    const float t0 = nowMs();
    const bool ok = provider->deserialize(id, layer, raw->bytes, raw->content);
    reservation->commit(nowMs() - t0);
    std::vector<uint8_t>().swap(raw->bytes); // free the raw bytes; stage 3 does not need them
    if (!ok) raw->failed.store(true, std::memory_order_release);
    raw->cpuDone.store(true, std::memory_order_release);
  });
}

void StreamingScheduler::pumpCpuToGpu(LayerLoad& load) {
  if (!load.cpuDone.load(std::memory_order_acquire)) return;
  const CellState stage = load.stage.load(std::memory_order_acquire);
  if (stage != CellState::LoadingCPU && stage != CellState::LoadingGPU) return;
  // Wait for the deserialize task to actually finish before touching its output on this thread.
  if (load.task.valid() && !load.task.finished()) return;

  // This runs on the main thread (update's caller), which owns the device — the one place GPU
  // resource creation is legal.
  load.stage.store(CellState::LoadingGPU, std::memory_order_release);

  bool ok = !load.failed.load(std::memory_order_acquire);
  const bool hasContent =
      load.content.cpuBytes > 0 || load.content.gpuBytes > 0 || load.content.userData;
  if (ok && hasContent) {
    auto reservation = m_budget.reserve(BudgetChannel::GPU);
    if (!reservation.valid()) return; // GPU budget spent this frame; upload next frame
    const float t0 = nowMs();
    ok = m_provider.upload(load.cell, load.layer, load.content);
    reservation.commit(nowMs() - t0);

    // Replay any mutations captured on a previous unload, before the layer is marked resident, so
    // the door is already open the moment the cell reappears — no pristine-state flash.
    if (ok && m_persistence) {
      CellDelta delta;
      if (m_persistence->get(load.cell, load.layer, delta)) {
        m_provider.applyDelta(load.cell, load.layer, load.content, delta);
      }
    }
  }

  TrackedCell* tracked = nullptr;
  auto it = m_tracked.find(load.cell.key());
  if (it != m_tracked.end()) tracked = &it->second;
  if (tracked) finishLayer(*tracked, load.layer, ok);
}

void StreamingScheduler::finishLayer(TrackedCell& tracked, WorldLayer layer, bool ok) {
  auto& slot = tracked.loads[uint32_t(layer)];
  if (!slot) return;

  m_inFlight.fetch_sub(1, std::memory_order_relaxed);

  WorldCell* cell = m_grid.find(tracked.id);
  if (ok) {
    tracked.loaded[uint32_t(layer)] = true;
    if (cell) {
      auto& cl = cell->layers[uint32_t(layer)];
      cl.state.store(CellState::Loaded, std::memory_order_release);
      cl.cpuBytes = slot->content.cpuBytes;
      cl.gpuBytes = slot->content.gpuBytes;
    }
    ++m_stats.loadsCompleted;
    // Keep the content pointer alive on the cell via the slot; the slot stays until unload.
    slot->stage.store(CellState::Loaded, std::memory_order_release);
  } else {
    if (cell) cell->layers[uint32_t(layer)].state.store(CellState::Failed, std::memory_order_release);
    ++m_stats.loadsFailed;
    slot.reset(); // let it be retried on a later frame
  }
}

void StreamingScheduler::releaseLayer(const CellId& id, WorldLayer layer, LayerLoad& load) {
  const bool hadContent =
      load.content.userData || load.content.cpuBytes > 0 || load.content.gpuBytes > 0;

  // Persist runtime mutations before the content is destroyed. Only for a layer that finished
  // loading — a cell abandoned mid-pipeline never became live and has nothing to capture.
  if (hadContent && m_persistence &&
      load.stage.load(std::memory_order_acquire) == CellState::Loaded) {
    CellDelta delta;
    if (m_provider.captureDelta(id, layer, load.content, delta) && !delta.empty()) {
      m_persistence->put(id, layer, std::move(delta));
    }
  }

  if (hadContent) {
    m_provider.release(id, layer, load.content);
  }
  load.content = {};
}

void StreamingScheduler::considerUnloads() {
  std::vector<StreamingObserver> observers;
  {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    observers = m_observers;
  }

  std::vector<uint64_t> toErase;
  for (auto& [key, tracked] : m_tracked) {
    WorldCell* cell = m_grid.find(tracked.id);
    if (!cell) {
      toErase.push_back(key);
      continue;
    }

    // Nearest observer, using the UNLOAD radius. The gap between load and unload radius is the
    // hysteresis: a cell stays resident until the observer is clearly past it.
    float nearest = 1e30f;
    for (const auto& obs : observers) {
      if (!obs.active) continue;
      nearest = std::min(nearest, cell->distanceTo(obs.position));
    }

    const bool wanted = [&] {
      for (const auto& obs : observers) {
        if (obs.active && cell->distanceTo(obs.position) <= obs.unloadRadius) return true;
      }
      return false;
    }();

    if (wanted) {
      tracked.lastWantedFrame = m_frame;
      continue;
    }

    // Grace period on top of the radius, so a single-frame flicker does not tear a cell down.
    if (m_frame - tracked.lastWantedFrame < m_desc.unloadGraceFrames) continue;

    // Abandon anything still loading, then release what finished.
    for (uint32_t l = 0; l < kLayerCount; ++l) {
      auto& slot = tracked.loads[l];
      if (!slot) continue;

      const CellState st = slot->stage.load(std::memory_order_acquire);
      if (isLoading(st) && st != CellState::Loaded) {
        slot->abandoned.store(true, std::memory_order_release);
        if (slot->task.valid() && !slot->task.finished()) {
          // Still running: leave the tracked cell in place and revisit next frame. Releasing now
          // would free a LayerLoad a worker still points at.
          goto nextCell;
        }
        m_inFlight.fetch_sub(1, std::memory_order_relaxed);
        ++m_stats.cancellations;
      }
      releaseLayer(tracked.id, WorldLayer(l), *slot);
      slot.reset();
    }

    // Everything for this cell is released; drop it from the grid and the tracker.
    m_grid.erase(tracked.id);
    toErase.push_back(key);
    ++m_stats.unloadsIssued;
  nextCell:;
  }

  for (uint64_t key : toErase) m_tracked.erase(key);
}

void StreamingScheduler::update(float lastFrameMs) {
  ++m_frame;
  m_budget.beginFrame(lastFrameMs);

  m_stats.loadsStarted = 0;
  m_stats.loadsCompleted = 0;
  m_stats.unloadsIssued = 0;

  // 0. Feed the predictors this frame's observer state, and retire predictors for observers that
  //    are no longer present so a returning id starts fresh instead of resuming stale motion.
  {
    const float dt = std::max(lastFrameMs, 0.0001f) / 1000.0f;
    std::lock_guard<std::mutex> lock(m_observerMutex);
    std::unordered_map<uint32_t, char> present;
    for (const auto& obs : m_observers) {
      if (!obs.active) continue;
      present[obs.id] = 1;
      m_predictors[obs.id].update(obs.position, obs.velocity, dt, m_desc.prediction);
    }
    for (auto it = m_predictors.begin(); it != m_predictors.end();) {
      it = present.count(it->first) ? std::next(it) : m_predictors.erase(it);
    }
  }

  // 1. Which cells do the observers want, most urgent first (prefetch included, at the tail).
  std::vector<std::pair<CellId, float>> wanted;
  gatherWantedCells(wanted);
  m_stats.cellsWanted = uint32_t(wanted.size());

  // 2. Make sure each wanted cell is tracked, and start its layers in weight order (gameplay
  //    first) as long as the concurrency cap and the budget allow.
  uint32_t missing = 0;
  for (const auto& [id, cellPriority] : wanted) {
    TrackedCell& tracked = m_tracked.try_emplace(id.key()).first->second;
    tracked.id = id;
    tracked.lastWantedFrame = m_frame;

    // A cell only reached through prediction has cellPriority in the penalty range; its layers
    // should enter the pipeline as prefetch (lower task priority), not as urgent loads.
    const bool prefetch = cellPriority >= kPrefetchPenalty;

    bool anyUnloaded = false;
    for (uint32_t l = 0; l < kLayerCount; ++l) {
      if (tracked.loaded[l]) continue;
      anyUnloaded = true;
      WorldCell& cell = m_grid.getOrCreate(id);
      // priorityOf returns 1e30 for a cell no observer can see (prefetch-only); fall back to the
      // cell's merged priority so ordering still reflects the prediction distance.
      float p = priorityOf(cell, WorldLayer(l));
      if (p >= 1e29f) p = cellPriority;
      startLayerLoad(tracked, WorldLayer(l), p, prefetch);
    }
    if (anyUnloaded) ++missing;
  }
  m_stats.cellsMissing = missing;

  // 3. Advance every in-flight layer one stage. Order matters: pump CPU→GPU last so a layer that
  //    finished stage 2 this frame can still reach the GPU this frame if budget remains.
  for (auto& [key, tracked] : m_tracked) {
    for (auto& slot : tracked.loads) {
      if (slot) pumpIoToCpu(*slot);
    }
  }
  for (auto& [key, tracked] : m_tracked) {
    for (auto& slot : tracked.loads) {
      if (slot) pumpCpuToGpu(*slot);
    }
  }

  // 4. Unload what has fallen out of range.
  considerUnloads();

  // 5. Stats.
  m_stats.cellsResident = 0;
  m_stats.cellsLoading = 0;
  m_stats.layersLoaded = 0;
  m_stats.cpuBytes = 0;
  m_stats.gpuBytes = 0;
  for (auto& [key, tracked] : m_tracked) {
    bool anyLoading = false;
    bool allLoaded = true;
    for (uint32_t l = 0; l < kLayerCount; ++l) {
      if (tracked.loaded[l]) {
        ++m_stats.layersLoaded;
        if (tracked.loads[l]) {
          m_stats.cpuBytes += tracked.loads[l]->content.cpuBytes;
          m_stats.gpuBytes += tracked.loads[l]->content.gpuBytes;
        }
      } else {
        allLoaded = false;
        if (tracked.loads[l]) anyLoading = true;
      }
    }
    if (allLoaded) ++m_stats.cellsResident;
    if (anyLoading) ++m_stats.cellsLoading;
  }
  m_stats.budgetRejections = m_budget.rejections();
}

void StreamingScheduler::drain() {
  // Pump until nothing is in flight. Generous budget so a test does not wait dozens of frames.
  auto& s = m_budget.settings();
  const auto saved = s;
  s.channelMs[uint32_t(BudgetChannel::IO)] = 1000.0f;
  s.channelMs[uint32_t(BudgetChannel::CPU)] = 1000.0f;
  s.channelMs[uint32_t(BudgetChannel::GPU)] = 1000.0f;
  s.totalMs = 3000.0f;

  for (int guard = 0; guard < 100000; ++guard) {
    update(1.0f);
    if (m_inFlight.load(std::memory_order_relaxed) == 0 && m_stats.cellsMissing == 0) break;
    std::this_thread::yield();
  }

  s = saved;
}

} // namespace tucano::world
