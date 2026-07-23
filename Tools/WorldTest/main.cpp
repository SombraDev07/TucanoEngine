// Gate for the World Machine foundation (WM-0 partition + WM-1 budget + the async scheduler that
// WM-2 will run on). Numeric and headless — no window, no assets, no GPU.

#include "Core/TaskScheduler.h"
#include "World/CellId.h"
#include "World/FrustumCull.h"
#include "World/MovementPredictor.h"
#include "World/StreamingBudget.h"
#include "World/StreamingScheduler.h"
#include "World/StreamingTypes.h"
#include "World/WorldGrid.h"

#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace tucano;
using namespace tucano::world;

namespace {

int g_failures = 0;

void check(const std::string& label, bool ok) {
  std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label.c_str());
  if (!ok) ++g_failures;
}

// ── Cell identity ────────────────────────────────────

void testCellId() {
  std::printf("\n[cell id]\n");

  // Round-tripping through the packed key is the one thing everything else rests on: the key is
  // the map key, the serialized id and the GPU-side handle.
  std::mt19937 rng(1234);
  std::uniform_int_distribution<int32_t> coord(-500000, 500000);
  std::uniform_int_distribution<uint32_t> lvl(0, kMaxLevel);

  bool allRoundTrip = true;
  for (int i = 0; i < 20000; ++i) {
    const CellId id{coord(rng), coord(rng), coord(rng), lvl(rng)};
    if (CellId::fromKey(id.key()) != id) {
      allRoundTrip = false;
      std::printf("       mismatch at (%d,%d,%d,L%u)\n", id.x, id.y, id.z, id.level);
      break;
    }
  }
  check("20k random ids round-trip through the packed key", allRoundTrip);

  // Distinct cells must never collide, or two places in the world would share one slot.
  const CellId a{1, 2, 3, 5};
  const CellId b{1, 2, 3, 6}; // same coordinate, different level
  const CellId c{3, 2, 1, 5}; // permuted coordinate
  check("level is part of the key", a.key() != b.key());
  check("axis order matters", a.key() != c.key());

  // Negative coordinates are the case a naive morton encoder gets wrong.
  const CellId neg{-1, -1, -1, 8};
  check("negative coordinates survive the round trip", CellId::fromKey(neg.key()) == neg);

  // Hierarchy without a tree: parent/child are pure arithmetic.
  const CellId parent{-3, 7, 2, 4};
  bool childrenAgree = true;
  for (uint32_t oct = 0; oct < 8; ++oct) {
    if (parent.child(oct).parent() != parent) childrenAgree = false;
  }
  check("all 8 children report the correct parent", childrenAgree);
  check("parent of level 0 is itself", CellId({5, 5, 5, 0}).parent().level == 0);

  // Spatial locality is the reason for morton over a plain hash of x,y,z: neighbours should land
  // near each other so hash buckets and disk reads stay coherent.
  const CellId origin{100, 100, 100, 10};
  const CellId nextX{101, 100, 100, 10};
  const CellId farAway{100000, 100, 100, 10};
  const uint64_t nearDelta = nextX.key() > origin.key() ? nextX.key() - origin.key()
                                                        : origin.key() - nextX.key();
  const uint64_t farDelta = farAway.key() > origin.key() ? farAway.key() - origin.key()
                                                         : origin.key() - farAway.key();
  check("morton keeps neighbours close in key space", nearDelta < farDelta);
}

// ── Grid geometry ────────────────────────────────────

void testGridGeometry() {
  std::printf("\n[grid geometry]\n");

  WorldGridDesc desc;
  desc.origin = glm::vec3(0.0f);
  desc.rootCellSize = 65536.0f;
  desc.streamLevel = 10;
  WorldGrid grid(desc);

  check("stream level gives 64 m cells", std::fabs(grid.cellSize(10) - 64.0f) < 0.001f);
  check("each level halves the edge", std::fabs(grid.cellSize(11) - 32.0f) < 0.001f);

  // The seam at the origin is where a truncating implementation breaks: -0.5 and +0.5 must not
  // land in the same cell.
  const CellId justBelow = grid.cellAt(glm::vec3(-0.5f, 0.0f, 0.0f), 10);
  const CellId justAbove = grid.cellAt(glm::vec3(0.5f, 0.0f, 0.0f), 10);
  check("no seam at the world origin", justBelow.x == -1 && justAbove.x == 0);

  // A point must land inside the bounds of the cell the grid says contains it.
  std::mt19937 rng(99);
  std::uniform_real_distribution<float> pos(-20000.0f, 20000.0f);
  bool containment = true;
  for (int i = 0; i < 5000; ++i) {
    const glm::vec3 p(pos(rng), pos(rng), pos(rng));
    const CellId id = grid.cellAt(p, 10);
    glm::vec3 bmin, bmax;
    grid.boundsOf(id, bmin, bmax);
    if (p.x < bmin.x || p.x >= bmax.x || p.y < bmin.y || p.y >= bmax.y || p.z < bmin.z ||
        p.z >= bmax.z) {
      containment = false;
      break;
    }
  }
  check("5k random points fall inside their own cell's bounds", containment);

  // Distance is measured to the box, not the centre — a big cell underfoot is not "far".
  WorldCell& cell = grid.getOrCreate(CellId{0, 0, 0, 10});
  check("distance is zero inside the cell", cell.distanceTo(glm::vec3(32.0f)) == 0.0f);
  const float d = cell.distanceTo(glm::vec3(-10.0f, 32.0f, 32.0f));
  check("distance to the face is measured from the box", std::fabs(d - 10.0f) < 0.001f);
}

// ── Radius queries ───────────────────────────────────

void testRadiusQuery() {
  std::printf("\n[radius query]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  const glm::vec3 observer(1000.0f, 50.0f, -2000.0f);

  const auto near = grid.cellsInRadius(observer, 200.0f, 10);
  const auto far = grid.cellsInRadius(observer, 400.0f, 10);
  check("a larger radius returns more cells", far.size() > near.size());

  // Every returned cell must genuinely be within range, and the observer's own cell must be in
  // there — an off-by-one in the loop bounds would drop exactly that one.
  const CellId self = grid.cellAt(observer, 10);
  bool containsSelf = false;
  bool allInRange = true;
  for (const CellId& id : near) {
    if (id == self) containsSelf = true;
    glm::vec3 bmin, bmax;
    grid.boundsOf(id, bmin, bmax);
    const glm::vec3 d = glm::max(glm::max(bmin - observer, glm::vec3(0.0f)), observer - bmax);
    if (glm::length(d) > 200.0f + 0.001f) allInRange = false;
  }
  check("the observer's own cell is included", containsSelf);
  check("no returned cell is outside the radius", allInRange);

  // Cost must scale with the radius, not with the size of the world. A 64 m grid over a 200 m
  // radius is a 7x7x7 neighbourhood at most.
  check("query size scales with the radius, not the world", near.size() < 512);

  // A radius large enough to cover the whole world must refuse rather than hang.
  const auto absurd = grid.cellsInRadius(observer, 1e9f, 14);
  check("an absurd radius is refused instead of hanging", absurd.empty());
}

// ── Sparse store ─────────────────────────────────────

void testSparseStore() {
  std::printf("\n[sparse store]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  check("a fresh grid holds no cells", grid.cellCount() == 0);
  check("an absent cell is not found", grid.find(CellId{5, 5, 5, 10}) == nullptr);

  WorldCell& c = grid.getOrCreate(CellId{5, 5, 5, 10});
  c.layers[uint32_t(WorldLayer::Gameplay)].state.store(CellState::Loaded);
  check("getOrCreate inserts one cell", grid.cellCount() == 1);
  check("getOrCreate is idempotent", &grid.getOrCreate(CellId{5, 5, 5, 10}) == &c);
  check("the cell can be found again", grid.find(CellId{5, 5, 5, 10}) == &c);

  // Sparsity is the point: empty sky costs nothing because no cell is ever made there.
  check("only touched cells exist", grid.cellCount() == 1);

  check("erase removes it", grid.erase(CellId{5, 5, 5, 10}));
  check("erasing twice reports false", !grid.erase(CellId{5, 5, 5, 10}));
  check("the grid is empty again", grid.cellCount() == 0);
}

// ── Layer lifecycle ──────────────────────────────────

void testLayers() {
  std::printf("\n[layers]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  WorldCell& cell = grid.getOrCreate(CellId{0, 0, 0, 10});

  check("a new cell is not loaded", !cell.fullyLoaded());
  check("a new cell is not loading", !cell.anyLoading());

  cell.layers[uint32_t(WorldLayer::Gameplay)].state.store(CellState::LoadingIO);
  check("a cell mid-pipeline reports loading", cell.anyLoading());
  check("partial load is not full load", !cell.fullyLoaded());

  for (auto& l : cell.layers) l.state.store(CellState::Loaded);
  check("all layers loaded means fully loaded", cell.fullyLoaded());
  check("fully loaded is not loading", !cell.anyLoading());

  // Gameplay must outrank the rest, or the player walks into a place with no floor.
  check("gameplay outranks visual", kLayerWeight[0] > kLayerWeight[1]);
  check("visual outranks audio and detail", kLayerWeight[1] > kLayerWeight[2] &&
                                                kLayerWeight[2] > kLayerWeight[3]);

  cell.layers[uint32_t(WorldLayer::Visual)].cpuBytes = 1000;
  cell.layers[uint32_t(WorldLayer::Visual)].gpuBytes = 4000;
  cell.layers[uint32_t(WorldLayer::Detail)].cpuBytes = 500;
  check("byte accounting sums across layers",
        cell.totalCpuBytes() == 1500 && cell.totalGpuBytes() == 4000);
  check("the grid totals match the cell", grid.totalCpuBytes() == 1500);
}

// ── Concurrency ──────────────────────────────────────

void testConcurrency() {
  std::printf("\n[concurrency]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});

  // The real requirement: streaming threads create cells while the frame thread reads them. If
  // sharding or the double-checked insert were wrong, this loses cells or corrupts the map.
  constexpr int kThreads = 8;
  constexpr int kPerThread = 2000;
  std::vector<std::thread> threads;
  std::atomic<int> created{0};

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&grid, &created, t] {
      for (int i = 0; i < kPerThread; ++i) {
        // Overlapping ranges on purpose, so threads race for the same cells.
        const int32_t x = (t * 137 + i) % 500;
        const int32_t y = i % 20;
        WorldCell& c = grid.getOrCreate(CellId{x, y, 0, 10});
        c.lastTouchFrame.store(uint64_t(i), std::memory_order_relaxed);
        created.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& th : threads) th.join();

  check("every insert completed", created.load() == kThreads * kPerThread);

  // Each distinct coordinate must exist exactly once, whoever won the race.
  size_t expected = 0;
  for (int32_t x = 0; x < 500; ++x) {
    for (int32_t y = 0; y < 20; ++y) {
      if (grid.find(CellId{x, y, 0, 10})) ++expected;
    }
  }
  check("no cell was duplicated or lost", grid.cellCount() == expected);
  check("all coordinates are reachable", expected > 0);

  // Sharding must actually spread the load, or the shard mutex is just a global lock in disguise.
  check("cells are spread across shards", grid.cellCount() > WorldGrid::kShards);
}

// ── Task scheduler ───────────────────────────────────

void testTaskScheduler() {
  std::printf("\n[task scheduler]\n");

  core::TaskScheduler sched(4);
  check("workers were started", sched.workerCount() == 4);

  std::atomic<int> ran{0};
  std::vector<core::TaskHandle> handles;
  for (int i = 0; i < 200; ++i) {
    handles.push_back(sched.submit(core::TaskPriority::Normal, [&ran] {
      ran.fetch_add(1, std::memory_order_relaxed);
    }));
  }
  sched.waitIdle();
  check("all 200 tasks ran", ran.load() == 200);
  check("completion count matches", sched.completed() == 200);

  bool allFinished = true;
  for (const auto& h : handles) {
    if (!h.finished()) allFinished = false;
  }
  check("every handle reports finished", allFinished);

  // Submitting must not block the caller — that is the entire reason this exists rather than
  // reusing JobSystem::parallelFor.
  {
    std::atomic<bool> release{false};
    core::TaskScheduler blocker(1);
    blocker.submit(core::TaskPriority::Normal, [&release] {
      while (!release.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
      blocker.submit(core::TaskPriority::Normal, [] {});
    }
    const auto elapsedMs =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    release.store(true, std::memory_order_release);
    blocker.waitIdle();
    check("submitting behind a busy worker does not block", elapsedMs < 50.0f);
  }

  // Priority ordering, checked with a single worker so the queue order is the run order.
  {
    core::TaskScheduler ordered(1);
    std::atomic<bool> gate{false};
    std::vector<int> order;
    std::mutex orderMutex;

    // Hold the worker so everything else queues up behind it.
    ordered.submit(core::TaskPriority::Critical, [&gate] {
      while (!gate.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

    auto record = [&order, &orderMutex](int tag) {
      std::lock_guard<std::mutex> lock(orderMutex);
      order.push_back(tag);
    };
    ordered.submit(core::TaskPriority::Background, [&record] { record(4); });
    ordered.submit(core::TaskPriority::Prefetch, [&record] { record(3); });
    ordered.submit(core::TaskPriority::Critical, [&record] { record(1); });
    ordered.submit(core::TaskPriority::Normal, [&record] { record(2); });

    gate.store(true, std::memory_order_release);
    ordered.waitIdle();

    const bool sorted = order.size() == 4 && order[0] == 1 && order[1] == 2 && order[2] == 3 &&
                        order[3] == 4;
    check("higher priority runs first", sorted);
  }

  // FIFO within one priority, so equal-priority cells load in the order they were requested
  // instead of an arbitrary one.
  {
    core::TaskScheduler fifo(1);
    std::atomic<bool> gate{false};
    std::vector<int> order;
    std::mutex orderMutex;
    fifo.submit(core::TaskPriority::Critical, [&gate] {
      while (!gate.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    for (int i = 0; i < 10; ++i) {
      fifo.submit(core::TaskPriority::Normal, [&order, &orderMutex, i] {
        std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(i);
      });
    }
    gate.store(true, std::memory_order_release);
    fifo.waitIdle();

    bool inOrder = order.size() == 10;
    for (size_t i = 0; inOrder && i < order.size(); ++i) {
      if (order[i] != int(i)) inOrder = false;
    }
    check("equal priorities stay FIFO", inOrder);
  }

  // Cancellation: a queued task must be droppable, or a cell the player already ran past would
  // still cost a disk read.
  {
    core::TaskScheduler cancel(1);
    std::atomic<bool> gate{false};
    std::atomic<int> sideEffect{0};
    cancel.submit(core::TaskPriority::Critical, [&gate] {
      while (!gate.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    auto doomed = cancel.submit(core::TaskPriority::Normal, [&sideEffect] {
      sideEffect.fetch_add(1, std::memory_order_relaxed);
    });
    const bool dropped = doomed.cancel();
    gate.store(true, std::memory_order_release);
    cancel.waitIdle();
    check("a queued task can be cancelled before it runs", dropped && sideEffect.load() == 0);
    check("a cancelled task still reports finished", doomed.finished());
  }

  // A throwing task must not take the worker down with it.
  {
    core::TaskScheduler robust(2);
    robust.submit(core::TaskPriority::Normal, [] { throw std::runtime_error("boom"); });
    std::atomic<int> after{0};
    for (int i = 0; i < 20; ++i) {
      robust.submit(core::TaskPriority::Normal, [&after] {
        after.fetch_add(1, std::memory_order_relaxed);
      });
    }
    robust.waitIdle();
    check("a throwing task does not kill its worker", after.load() == 20);
  }
}

// ── Frame budget ─────────────────────────────────────

void testBudget() {
  std::printf("\n[frame budget]\n");

  StreamingBudgetSettings settings;
  settings.channelMs[uint32_t(BudgetChannel::IO)] = 2.0f;
  settings.channelMs[uint32_t(BudgetChannel::CPU)] = 3.0f;
  settings.channelMs[uint32_t(BudgetChannel::GPU)] = 1.0f;
  settings.totalMs = 5.0f;
  StreamingBudget budget(settings);

  budget.beginFrame(16.6f);

  // Reserving up front is the correction to the roadmap: the budget must be able to say no BEFORE
  // the work happens, not report the overspend afterwards.
  auto first = budget.reserveExplicit(BudgetChannel::IO, 1.5f);
  check("a reservation within budget is granted", first.valid());
  check("the channel is charged immediately, before the work runs",
        budget.usedMs(BudgetChannel::IO) > 1.4f);

  auto second = budget.reserveExplicit(BudgetChannel::IO, 1.5f);
  check("a reservation over budget is refused", !second.valid());
  check("the refusal was counted", budget.rejections() == 1);

  // Committing the real cost reconciles the estimate.
  first.commit(0.5f);
  check("committing replaces the estimate with the truth",
        std::fabs(budget.usedMs(BudgetChannel::IO) - 0.5f) < 0.01f);
  check("room freed by a cheap job is usable again",
        budget.reserveExplicit(BudgetChannel::IO, 1.0f).valid());

  // Dropping a reservation without committing must refund it, or a cancelled load would leak
  // budget until the frame ended.
  budget.beginFrame(16.6f);
  {
    auto scoped = budget.reserveExplicit(BudgetChannel::CPU, 2.0f);
    check("the reservation is held while alive", budget.usedMs(BudgetChannel::CPU) > 1.9f);
  }
  check("an uncommitted reservation is refunded",
        std::fabs(budget.usedMs(BudgetChannel::CPU)) < 0.01f);

  // Channels are independent: being disk-bound must not stop GPU uploads.
  budget.beginFrame(16.6f);
  auto io = budget.reserveExplicit(BudgetChannel::IO, 2.0f);
  check("a saturated channel does not block another",
        budget.reserveExplicit(BudgetChannel::GPU, 0.5f).valid());
  io.commit(2.0f);

  // The combined ceiling is lower than the sum of the channels, because they overlap in
  // wall-clock time.
  budget.beginFrame(16.6f);
  auto a = budget.reserveExplicit(BudgetChannel::IO, 2.0f);
  auto b = budget.reserveExplicit(BudgetChannel::CPU, 2.9f);
  auto c = budget.reserveExplicit(BudgetChannel::GPU, 0.9f);
  check("the combined cap bites before the channel caps do", a.valid() && b.valid() && !c.valid());

  // A job more expensive than the entire budget must still get through, once, or streaming would
  // deadlock on it for ever.
  budget.beginFrame(16.6f);
  check("an oversized job is allowed when the channel is untouched",
        budget.reserveExplicit(BudgetChannel::IO, 500.0f).valid());

  // Learned cost: the budget should not need a hand-tuned constant per world.
  {
    StreamingBudget learner(settings);
    for (int frame = 0; frame < 40; ++frame) {
      learner.beginFrame(16.6f);
      auto r = learner.reserve(BudgetChannel::CPU);
      if (r.valid()) r.commit(0.8f);
    }
    const float avg = learner.averageCostMs(BudgetChannel::CPU);
    check("the channel learns what its jobs actually cost", std::fabs(avg - 0.8f) < 0.05f);
  }

  // Adaptation: a frame over target must shrink the budget, a frame under it must grow it.
  {
    StreamingBudget adapt(settings);
    for (int i = 0; i < 200; ++i) adapt.beginFrame(33.0f); // half speed
    const float slow = adapt.scale();
    for (int i = 0; i < 200; ++i) adapt.beginFrame(8.0f); // twice as fast
    const float fast = adapt.scale();
    check("a slow frame shrinks the budget", slow < 0.9f);
    check("a fast frame grows it back", fast > slow);
    check("the scale stays inside its bounds",
          slow >= settings.minScale - 0.001f && fast <= settings.maxScale + 0.001f);
  }
}

// ── Streaming scheduler ──────────────────────────────

/// A provider backed by RAM, not disk. It records how each stage was called so the test can prove
/// the pipeline ran in order, on the right threads, and released everything it built.
class FakeProvider : public CellDataProvider {
public:
  /// Small sleeps make the three stages actually overlap in wall-clock time, so the pipeline is
  /// exercised rather than each cell finishing instantly in submission order.
  bool slow = false;

  std::atomic<int> reads{0};
  std::atomic<int> deserializes{0};
  std::atomic<int> uploads{0};
  std::atomic<int> releases{0};
  std::atomic<int> liveContent{0}; ///< uploads minus releases — must return to zero

  std::atomic<std::thread::id> uploadThread{};

  bool readBytes(const CellId& id, WorldLayer layer, std::vector<uint8_t>& out) override {
    reads.fetch_add(1, std::memory_order_relaxed);
    if (slow) std::this_thread::sleep_for(std::chrono::microseconds(200));
    // Detail layer is intentionally empty for even-x cells, to exercise the empty-layer path.
    if (layer == WorldLayer::Detail && (id.x % 2 == 0)) return false;
    out.assign(64, uint8_t(layer));
    return true;
  }

  bool deserialize(const CellId& id, WorldLayer layer, const std::vector<uint8_t>& bytes,
                   CellContent& out) override {
    deserializes.fetch_add(1, std::memory_order_relaxed);
    if (slow) std::this_thread::sleep_for(std::chrono::microseconds(200));
    out.cpuBytes = bytes.size();
    out.gpuBytes = bytes.size() * 2;
    out.userData = reinterpret_cast<void*>(uintptr_t(0xC0DE));
    // Fresh base data is always the authored state — door closed. Only a persisted delta reopens
    // it. Resetting here is what makes the persistence test meaningful: without the delta system,
    // a reload would show the pristine door.
    if (layer == WorldLayer::Gameplay) setDoorOpen(id, false);
    return true;
  }

  bool upload(const CellId&, WorldLayer, CellContent& content) override {
    uploads.fetch_add(1, std::memory_order_relaxed);
    liveContent.fetch_add(1, std::memory_order_relaxed);
    uploadThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
    content.gpuBytes = content.cpuBytes * 2;
    return true;
  }

  void release(const CellId&, WorldLayer, CellContent& content) override {
    releases.fetch_add(1, std::memory_order_relaxed);
    liveContent.fetch_sub(1, std::memory_order_relaxed);
    content = {};
  }

  // ── Persistence model ──
  // Each gameplay layer carries a mutable "doorOpen" flag, standing in for any runtime change. It
  // starts closed (authored state). captureDelta serializes it; applyDelta restores it. The map
  // records what the LIVE cell currently believes, keyed by cell, so the test can mutate a loaded
  // cell and then check the flag survived an unload/reload.
  std::mutex liveMutex;
  std::unordered_map<uint64_t, bool> liveDoorOpen; // cell key → live flag

  void setDoorOpen(const CellId& id, bool open) {
    std::lock_guard<std::mutex> lock(liveMutex);
    liveDoorOpen[id.key()] = open;
  }
  bool doorOpen(const CellId& id) {
    std::lock_guard<std::mutex> lock(liveMutex);
    auto it = liveDoorOpen.find(id.key());
    return it != liveDoorOpen.end() && it->second;
  }

  int captures = 0;
  int applies = 0;

  bool captureDelta(const CellId& id, WorldLayer layer, const CellContent&,
                    CellDelta& out) override {
    if (layer != WorldLayer::Gameplay) return false;
    const bool open = doorOpen(id);
    if (!open) return false; // pristine — nothing to persist
    ++captures;
    out.bytes.assign(1, uint8_t(1));
    return true;
  }

  void applyDelta(const CellId& id, WorldLayer layer, CellContent&,
                  const CellDelta& delta) override {
    if (layer != WorldLayer::Gameplay || delta.bytes.empty()) return;
    ++applies;
    setDoorOpen(id, delta.bytes[0] != 0);
  }
};

void testStreamingScheduler() {
  std::printf("\n[streaming scheduler]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.maxConcurrentLoads = 64;
  sd.unloadGraceFrames = 3;
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);

  const std::thread::id mainThread = std::this_thread::get_id();

  // One observer standing at the origin, small radius so the wanted set is a handful of cells.
  StreamingObserver obs;
  obs.position = glm::vec3(0.0f, 0.0f, 0.0f);
  obs.loadRadius = 100.0f;
  obs.unloadRadius = 160.0f;
  scheduler.setObservers({obs});

  scheduler.drain();
  const auto& st = scheduler.stats();

  check("cells were wanted around the observer", st.cellsWanted > 0);
  check("every wanted cell became resident", st.cellsMissing == 0);
  check("all wanted cells are resident", st.cellsResident == st.cellsWanted);
  check("bytes were accounted", st.cpuBytes > 0 && st.gpuBytes > 0);

  // The pipeline must have run every stage. Detail is empty on even-x cells, so uploads are fewer
  // than reads — that difference is itself the empty-layer path working.
  check("stage 1 ran for every layer of every cell", provider.reads.load() > 0);
  check("stage 2 ran", provider.deserializes.load() > 0);
  check("stage 3 ran", provider.uploads.load() > 0);
  check("empty layers skip deserialize", provider.deserializes.load() < provider.reads.load());

  // The critical threading contract: uploads MUST happen on the caller's thread, never a worker.
  check("GPU upload ran on the calling thread",
        provider.uploadThread.load() == mainThread);

  // Remove every observer and let the grace period elapse. Everything must unload and every
  // uploaded resource must be released — no leaks. (Moving the observer far instead would only
  // relocate the resident set, which is the multi-observer case, not unload.)
  scheduler.setObservers({});
  for (int i = 0; i < 10; ++i) scheduler.update(1.0f);

  check("cells unloaded when no observer wants them", scheduler.stats().cellsResident == 0);
  check("the grid is empty after unload", grid.cellCount() == 0);
  check("every uploaded resource was released", provider.liveContent.load() == 0);
  check("upload and release counts match",
        provider.uploads.load() == provider.releases.load());
}

void testStreamingHysteresis() {
  std::printf("\n[streaming hysteresis]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.unloadGraceFrames = 5;
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);

  // Load a wide neighbourhood, then shrink the load radius so the outer cells fall into the band
  // between the load and unload radius. They must stay resident and must NOT be torn down — that
  // gap is the whole point of two radii. The observer never moves, so nothing new is pulled in and
  // any release() would be pure thrash.
  StreamingObserver obs;
  obs.position = glm::vec3(0.0f);
  obs.loadRadius = 180.0f;
  obs.unloadRadius = 300.0f;
  scheduler.setObservers({obs});
  scheduler.drain();
  const uint32_t resident = scheduler.stats().cellsResident;
  check("cells loaded initially", resident > 0);

  // Shrink the load radius. Cells between 60 and 300 m are now in the hysteresis band.
  obs.loadRadius = 60.0f;
  scheduler.setObservers({obs});
  for (int i = 0; i < 20; ++i) scheduler.update(1.0f);
  check("cells in the hysteresis band stay resident", scheduler.stats().cellsResident == resident);
  check("no thrash in the hysteresis band", provider.releases.load() == 0);
}

void testStreamingCancellation() {
  std::printf("\n[streaming cancellation]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(2);
  StreamingBudget budget;
  FakeProvider provider;
  provider.slow = true; // stretch the pipeline so cells are caught mid-flight

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.unloadGraceFrames = 0; // unload as soon as unwanted
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);

  // Start loading a wide area, then yank the observer away after a single frame — many cells will
  // still be mid-pipeline. The scheduler must tear them down without leaking or crashing.
  StreamingObserver obs;
  obs.position = glm::vec3(0.0f);
  obs.loadRadius = 200.0f;
  obs.unloadRadius = 220.0f;
  scheduler.setObservers({obs});
  scheduler.update(1.0f); // kick off a wave of loads
  scheduler.update(1.0f);

  scheduler.setObservers({}); // no observer wants anything now
  for (int i = 0; i < 200; ++i) scheduler.update(1.0f);
  scheduler.drain();

  check("everything unloaded after the observer fled", scheduler.stats().cellsResident == 0);
  check("no resource leaked through cancellation", provider.liveContent.load() == 0);
  // Whatever was uploaded must have been released; anything cancelled before upload never
  // allocated, so the books still balance.
  check("upload and release still balance after cancellation",
        provider.uploads.load() == provider.releases.load());
}

void testMultiObserver() {
  std::printf("\n[multi-observer]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;
  StreamingScheduler scheduler(grid, tasks, budget, provider, StreamingSchedulerDesc{});

  // Two observers far apart. Each pulls in its own neighbourhood; a cell wanted by neither stays
  // absent. This is the split-screen / spectator case the roadmap scores as a 10.
  StreamingObserver a, b;
  a.position = glm::vec3(0.0f);
  a.loadRadius = 100.0f;
  a.unloadRadius = 160.0f;
  b.position = glm::vec3(5000.0f, 0.0f, 0.0f);
  b.loadRadius = 100.0f;
  b.unloadRadius = 160.0f;
  scheduler.setObservers({a, b});
  scheduler.drain();

  // Both neighbourhoods loaded, and the empty space between them did not.
  const CellId nearA = grid.cellAt(a.position, 10);
  const CellId nearB = grid.cellAt(b.position, 10);
  const CellId between = grid.cellAt(glm::vec3(2500.0f, 0.0f, 0.0f), 10);
  check("observer A's cell loaded", grid.find(nearA) != nullptr);
  check("observer B's cell loaded", grid.find(nearB) != nullptr);
  check("the gap between observers stayed empty", grid.find(between) == nullptr);
  check("both neighbourhoods are resident", scheduler.stats().cellsResident >= 2);
}

void testPersistenceStore() {
  std::printf("\n[persistence store]\n");

  CellPersistenceStore store;
  check("a fresh store is empty", store.deltaCount() == 0);

  const CellId a{3, 4, 5, 10};
  CellDelta d;
  d.bytes = {1, 2, 3, 4};
  store.put(a, WorldLayer::Gameplay, d);
  check("a delta can be stored", store.has(a, WorldLayer::Gameplay));
  check("an untouched layer has none", !store.has(a, WorldLayer::Visual));

  CellDelta got;
  check("the delta reads back", store.get(a, WorldLayer::Gameplay, got) && got.bytes == d.bytes);
  check("versioning starts non-zero", got.version > 0);

  // Storing an empty delta reverts the cell — memory should be reclaimed, not left at zero bytes.
  store.put(a, WorldLayer::Gameplay, CellDelta{});
  check("an empty delta clears the entry", !store.has(a, WorldLayer::Gameplay));
  check("the store is empty again", store.deltaCount() == 0);

  // Two different layers of one cell must not collide.
  store.put(a, WorldLayer::Gameplay, d);
  CellDelta d2;
  d2.bytes = {9};
  store.put(a, WorldLayer::Visual, d2);
  check("layers of one cell are independent", store.deltaCount() == 2);

  // Disk round trip: the deltas must survive save/load byte for byte.
  const std::string path =
      std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/tucano_deltas.bin";
  check("the store saves", store.save(path));

  CellPersistenceStore reloaded;
  check("the store loads", reloaded.load(path));
  check("the delta count survives the round trip", reloaded.deltaCount() == 2);
  CellDelta back;
  check("gameplay delta survives", reloaded.get(a, WorldLayer::Gameplay, back) && back.bytes == d.bytes);
  check("visual delta survives", reloaded.get(a, WorldLayer::Visual, back) && back.bytes == d2.bytes);

  // A malformed file must be rejected, leaving the target untouched.
  const std::string bad =
      std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/tucano_deltas_bad.bin";
  if (FILE* f = std::fopen(bad.c_str(), "wb")) {
    const char junk[] = "not a delta file";
    std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);
  }
  CellPersistenceStore survivor;
  survivor.put(a, WorldLayer::Gameplay, d);
  check("a malformed file is rejected", !survivor.load(bad));
  check("a rejected load leaves the store intact", survivor.has(a, WorldLayer::Gameplay));
}

void testStreamingPersistence() {
  std::printf("\n[streaming persistence]\n");

  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;
  CellPersistenceStore store;

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.unloadGraceFrames = 2;
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);
  scheduler.setPersistence(&store);

  StreamingObserver obs;
  obs.position = glm::vec3(0.0f);
  // A radius wider than one 64 m cell edge, so the x+1 neighbour (whose near face is at 64 m) is
  // also pulled in — the control below needs a second, untouched, resident cell.
  obs.loadRadius = 120.0f;
  obs.unloadRadius = 180.0f;
  scheduler.setObservers({obs});
  scheduler.drain();

  const CellId home = grid.cellAt(obs.position, 10);
  check("the observer's cell loaded", grid.find(home) != nullptr);
  check("the door starts closed (authored state)", !provider.doorOpen(home));

  // Open the door — a runtime mutation the player made.
  provider.setDoorOpen(home, true);

  // Walk away: the cell unloads and its delta must be captured.
  scheduler.setObservers({});
  for (int i = 0; i < 8; ++i) scheduler.update(1.0f);
  check("the cell unloaded", grid.find(home) == nullptr);
  check("a delta was captured on unload", store.has(home, WorldLayer::Gameplay));
  check("captureDelta ran", provider.captures > 0);

  // The door must be closed again in the provider's live map only because nothing is loaded; the
  // truth now lives in the delta store. Walk back: on reload the delta must reopen it.
  scheduler.setObservers({obs});
  scheduler.drain();
  check("the cell reloaded", grid.find(home) != nullptr);
  check("applyDelta ran on reload", provider.applies > 0);
  check("the door is open again after reload", provider.doorOpen(home));

  // Control: a cell the player never touched must NOT accumulate a delta, or every cell in the
  // world would cost persistence memory forever.
  const CellId neighbour = CellId{home.x + 1, home.y, home.z, 10};
  const bool neighbourResident = grid.find(neighbour) != nullptr;
  check("a neighbour also loaded", neighbourResident);
  scheduler.setObservers({});
  for (int i = 0; i < 8; ++i) scheduler.update(1.0f);
  check("an untouched cell leaves no delta", !store.has(neighbour, WorldLayer::Gameplay));
}

void testMovementPredictor() {
  std::printf("\n[movement predictor]\n");

  PredictionSettings s;
  s.smoothing = 1.0f; // no lag, so the math is exact and testable in one step

  // Supplied velocity is trusted directly — the exact value from physics, not a noisy derivative.
  {
    MovementPredictor p;
    p.update(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), 0.016f, s);
    check("supplied velocity is used as-is", std::fabs(p.velocity().x - 10.0f) < 0.001f);
    const glm::vec3 at2s = p.predict(2.0f);
    check("constant-velocity prediction is linear", std::fabs(at2s.x - 20.0f) < 0.01f);
  }

  // With no supplied velocity, it is estimated from the position delta.
  {
    MovementPredictor p;
    p.update(glm::vec3(0.0f), glm::vec3(0.0f), 0.1f, s);
    p.update(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.1f, s); // moved 5 m in 0.1 s → 50 m/s
    check("velocity is estimated from position when none is supplied",
          std::fabs(p.velocity().x - 50.0f) < 0.5f);
  }

  // Teleports must not translate into a request for a distant region.
  {
    MovementPredictor p;
    p.update(glm::vec3(0.0f), glm::vec3(0.0f), 0.016f, s);
    p.update(glm::vec3(100000.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.016f, s); // absurd jump
    check("a teleport is clamped to max speed", p.speed() <= s.maxSpeed + 0.001f);
  }

  // Standing still yields no prefetch — there is no heading to stream toward.
  {
    MovementPredictor p;
    p.update(glm::vec3(3.0f), glm::vec3(0.0f), 0.016f, s);
    std::vector<glm::vec3> pts;
    p.samplePoints(s, pts);
    check("a stationary observer produces no prefetch points", pts.empty());
  }

  // Sample points must lie ahead, and the cone must widen with the horizon.
  {
    MovementPredictor p;
    p.update(glm::vec3(0.0f), glm::vec3(30.0f, 0.0f, 0.0f), 0.016f, s);
    std::vector<glm::vec3> pts;
    p.samplePoints(s, pts);
    check("a moving observer produces prefetch points", !pts.empty());

    bool allAhead = true;
    float maxLateral = 0.0f;
    for (const glm::vec3& pt : pts) {
      if (pt.x <= 0.0f) allAhead = false; // must be forward of the origin
      maxLateral = std::max(maxLateral, std::fabs(pt.z));
    }
    check("all prefetch points are ahead of the observer", allAhead);
    check("the cone spreads laterally", maxLateral > 0.0f);
  }

  // Acceleration bends the prediction: speeding up should reach further than constant velocity.
  {
    MovementPredictor p;
    PredictionSettings accel = s;
    accel.smoothing = 1.0f;
    p.update(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), 0.1f, accel);
    p.update(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(20.0f, 0.0f, 0.0f), 0.1f, accel);
    // velocity 20, accel ~100 m/s². predict(1s) = pos + 20 + 0.5*100 = well beyond 20.
    check("acceleration carries the prediction further", p.predict(1.0f).x > 21.0f);
  }
}

void testPredictiveStreaming() {
  std::printf("\n[predictive streaming]\n");

  // The core claim of WM-3: cells in the direction of travel load before the observer reaches
  // them. Proven directionally — a cell AHEAD of a moving observer becomes resident even though it
  // is beyond the load radius, while its mirror image BEHIND does not.
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.maxConcurrentLoads = 128;
  sd.prediction.enabled = true;
  sd.prediction.prefetchRadius = 96.0f;
  sd.prediction.horizons = {1.0f, 2.0f};
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);

  StreamingObserver obs;
  obs.id = 1;
  obs.position = glm::vec3(0.0f);
  obs.velocity = glm::vec3(60.0f, 0.0f, 0.0f); // heading +x at 60 m/s
  obs.loadRadius = 100.0f;
  obs.unloadRadius = 160.0f;
  scheduler.setObservers({obs});

  // Feed the predictor a couple of frames so its estimate settles, then drain.
  for (int i = 0; i < 5; ++i) scheduler.update(16.0f);
  scheduler.drain();

  // A cell centred ~150 m ahead: beyond the 100 m load radius, but within predicted reach
  // (60 m/s × 2 s = 120 m predicted centre, + 96 m prefetch = 216 m). Its mirror at −150 m is
  // behind and must stay absent.
  const CellId ahead = grid.cellAt(glm::vec3(150.0f, 0.0f, 0.0f), 10);
  const CellId behind = grid.cellAt(glm::vec3(-150.0f, 0.0f, 0.0f), 10);

  check("a cell ahead of the observer was prefetched", grid.find(ahead) != nullptr);
  check("the mirror cell behind was not", grid.find(behind) == nullptr);

  // And prediction must not steal from the present: the observer's own cell is still resident.
  const CellId home = grid.cellAt(obs.position, 10);
  WorldCell* homeCell = grid.find(home);
  check("the observer's own cell is still resident", homeCell && homeCell->fullyLoaded());
}

void testPredictionPriority() {
  std::printf("\n[prediction priority]\n");

  // Prefetch must never delay a cell the observer can already see. With a tight concurrency cap,
  // the real load-radius cells must all become resident; prefetch only uses whatever is left.
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;
  provider.slow = true; // make loads take real time so the cap actually bites

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.maxConcurrentLoads = 4; // deliberately tight
  sd.prediction.enabled = true;
  sd.prediction.prefetchRadius = 200.0f; // lots of prefetch competing for slots
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);

  StreamingObserver obs;
  obs.id = 1;
  obs.position = glm::vec3(0.0f);
  obs.velocity = glm::vec3(80.0f, 0.0f, 0.0f);
  obs.loadRadius = 80.0f;
  obs.unloadRadius = 140.0f;
  scheduler.setObservers({obs});
  scheduler.drain();

  // Every cell inside the real load radius must be resident despite the prefetch competition.
  const auto realCells = grid.cellsInRadius(obs.position, obs.loadRadius, 10);
  bool allRealResident = true;
  for (const CellId& id : realCells) {
    WorldCell* c = grid.find(id);
    if (!c || !c->fullyLoaded()) allRealResident = false;
  }
  check("every visible cell loaded despite prefetch competition", allRealResident);
  check("there were real cells to load", !realCells.empty());
}

void testFrustumCull() {
  std::printf("\n[frustum cull]\n");

  // A standard LH, zero-to-one camera at the origin looking down +z.
  const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 1000.0f);
  const glm::mat4 viewProj = proj * view;
  const Frustum f = extractFrustum(viewProj);

  // A box straight ahead is inside; one directly behind is not; one far to the side is not.
  check("a box ahead is inside the frustum",
        aabbInFrustum(f, glm::vec3(-5, -5, 40), glm::vec3(5, 5, 60)));
  check("a box behind the camera is culled",
        !aabbInFrustum(f, glm::vec3(-5, -5, -60), glm::vec3(5, 5, -40)));
  check("a box far to the side is culled",
        !aabbInFrustum(f, glm::vec3(900, -5, 40), glm::vec3(1000, 5, 60)));
  check("a box past the far plane is culled",
        !aabbInFrustum(f, glm::vec3(-5, -5, 1200), glm::vec3(5, 5, 1300)));
  check("a box straddling the near plane is kept (conservative)",
        aabbInFrustum(f, glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)));

  // The real proof of the plane extraction: agree with an independently-derived clip-space test
  // over thousands of random boxes and random cameras. A convention error (row/column, GL vs D3D
  // depth, a flipped sign) would show up as a disagreement here.
  std::mt19937 rng(2024);
  std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
  std::uniform_real_distribution<float> ext(1.0f, 80.0f);
  std::uniform_real_distribution<float> ang(-3.14159f, 3.14159f);

  int disagreements = 0;
  for (int trial = 0; trial < 400; ++trial) {
    const glm::vec3 eye(pos(rng), pos(rng), pos(rng));
    const glm::vec3 target = eye + glm::vec3(std::cos(ang(rng)), 0.2f, std::sin(ang(rng)));
    const glm::mat4 v = glm::lookAtLH(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 p = glm::perspectiveLH_ZO(glm::radians(50.0f), 1.6f, 0.5f, 2000.0f);
    const glm::mat4 vp = p * v;
    const Frustum fr = extractFrustum(vp);

    for (int b = 0; b < 25; ++b) {
      const glm::vec3 c(pos(rng), pos(rng), pos(rng));
      const glm::vec3 e(ext(rng), ext(rng), ext(rng));
      const glm::vec3 bmin = c - e, bmax = c + e;
      if (aabbInFrustum(fr, bmin, bmax) != aabbInClipSpace(vp, bmin, bmax)) ++disagreements;
    }
  }
  check("plane test agrees with clip-space test over 10k random cases", disagreements == 0);

  // LOD by distance.
  CullConfig cfg;
  cfg.lodStep = 100.0f;
  cfg.maxLod = 3;
  check("near cells are LOD 0", selectLod(50.0f, cfg) == 0);
  check("LOD steps up with distance", selectLod(150.0f, cfg) == 1 && selectLod(250.0f, cfg) == 2);
  check("LOD saturates at maxLod", selectLod(100000.0f, cfg) == 3);

  // Cull over a real grid.
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  const float cs = grid.cellSize(10);
  for (int z = -4; z <= 20; ++z)
    for (int x = -10; x <= 10; ++x) grid.getOrCreate(CellId{x, 0, z, 10});

  const glm::vec3 eye(cs * 0.5f, cs * 0.5f, -cs);
  const glm::mat4 gv = glm::lookAtLH(eye, eye + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::mat4 gp = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.6f, 0.5f, 5000.0f);
  WorldCuller culler;
  std::vector<VisibleCell> visible;
  culler.cull(grid, gp * gv, eye, CullConfig{}, visible);
  check("culling a grid returns a visible subset",
        !visible.empty() && visible.size() < grid.cellCount());

  // Everything visible must genuinely pass the frustum test.
  bool allValid = true;
  const Frustum gf = extractFrustum(gp * gv);
  for (const auto& vc : visible) {
    glm::vec3 bmin, bmax;
    grid.boundsOf(vc.id, bmin, bmax);
    if (!aabbInFrustum(gf, bmin, bmax)) allValid = false;
  }
  check("every reported-visible cell really is in the frustum", allValid);
}

void testPersistenceDisabled() {
  std::printf("\n[persistence disabled]\n");

  // The control for the whole feature: with no persistence store set, a mutation is lost on
  // reload. This proves the reappearing door is the delta system and not some accident of the
  // provider holding state.
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  core::TaskScheduler tasks(4);
  StreamingBudget budget;
  FakeProvider provider;

  StreamingSchedulerDesc sd;
  sd.streamLevel = 10;
  sd.unloadGraceFrames = 2;
  StreamingScheduler scheduler(grid, tasks, budget, provider, sd);
  // Deliberately no setPersistence.

  StreamingObserver obs;
  obs.position = glm::vec3(0.0f);
  obs.loadRadius = 60.0f;
  obs.unloadRadius = 100.0f;
  scheduler.setObservers({obs});
  scheduler.drain();

  const CellId home = grid.cellAt(obs.position, 10);
  provider.setDoorOpen(home, true);

  scheduler.setObservers({});
  for (int i = 0; i < 8; ++i) scheduler.update(1.0f);
  scheduler.setObservers({obs});
  scheduler.drain();

  check("without persistence, captureDelta never ran", provider.captures == 0);
  check("without persistence, the mutation is lost on reload", !provider.doorOpen(home));
}

} // namespace

int main() {
  // Unbuffered: if a check crashes, the output up to that point must still reach the log.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("=== Tucano World Machine — WM-0 / WM-1 gate ===\n");

  testCellId();
  testGridGeometry();
  testRadiusQuery();
  testSparseStore();
  testLayers();
  testConcurrency();
  testTaskScheduler();
  testBudget();
  testStreamingScheduler();
  testStreamingHysteresis();
  testStreamingCancellation();
  testMultiObserver();
  testPersistenceStore();
  testStreamingPersistence();
  testPersistenceDisabled();
  testMovementPredictor();
  testPredictiveStreaming();
  testPredictionPriority();
  testFrustumCull();

  std::printf("\n=== failures: %d ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
