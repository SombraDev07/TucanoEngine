#pragma once

// Inputs and extension points of the streaming scheduler.
//
// The World module deliberately knows nothing about meshes, ECS entities or GPU buffers. It moves
// opaque payloads through a pipeline and lets the game decide what they are. That keeps the
// partition free of renderer dependencies, and — just as importantly — it is what makes the whole
// scheduler testable without a device, a disk or an asset.

#include "World/CellId.h"
#include "World/WorldGrid.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace tucano::world {

/// Something the world streams around. Usually the player, but also spectator cameras, split
/// screen viewpoints, or a server tracking every connected client.
struct StreamingObserver {
  /// Stable identity across frames. The predictor keys each observer's motion history on this, so
  /// two different observers (e.g. split-screen players) MUST carry different ids — otherwise their
  /// positions blur into one jittering history and prediction fans in a bogus direction.
  uint32_t id = 0;

  glm::vec3 position{0.0f};
  /// Metres per second. Used by the predictor in WM-3; already carried here so observers do not
  /// need a second plumbing pass later.
  glm::vec3 velocity{0.0f};

  /// Cells whose box is within this distance get loaded.
  float loadRadius = 256.0f;
  /// Cells stay resident until they are beyond this. The gap between the two radii is the
  /// hysteresis that stops a cell thrashing when the player walks back and forth over a boundary.
  float unloadRadius = 384.0f;

  /// Scales this observer's urgency. 1 for the player; higher values make an observer's cells load
  /// later, which is what a distant spectator camera should do.
  float priorityBias = 1.0f;

  bool active = true;
};

/// One layer's payload, as handed between pipeline stages. The scheduler never looks inside.
struct CellContent {
  /// Owned by the provider. The scheduler only passes it along and hands it back to release().
  void* userData = nullptr;
  uint64_t cpuBytes = 0;
  uint64_t gpuBytes = 0;
};

/// Where cell data comes from, split along the three pipeline stages.
///
/// The split is not cosmetic — it is what lets three cells be in flight at once, each on a
/// different resource. Which thread calls which method is part of the contract:
///
///   readBytes    — a TaskScheduler worker. May block on disk. Must be thread-safe.
///   deserialize  — a TaskScheduler worker. CPU-bound. Must be thread-safe.
///   upload       — the thread that called StreamingScheduler::update(), i.e. the one that owns
///                  the graphics device. Does NOT need to be thread-safe.
///   release      — same thread as upload.
///
/// The roadmap's diagram puts all three stages on worker threads. Stage 3 cannot go there in this
/// engine: the RHI is single-threaded, so creating buffers off the main thread would corrupt the
/// device. Draining stage 3 from update() under the GPU budget gets the same overlap anyway,
/// because stages 1 and 2 of later cells keep running while stage 3 of an earlier one waits.
class CellDataProvider {
public:
  virtual ~CellDataProvider() = default;

  /// Stage 1: pull raw bytes for one layer. Return false if the layer does not exist for this
  /// cell — that is not an error, it just means there is nothing to load.
  virtual bool readBytes(const CellId& id, WorldLayer layer, std::vector<uint8_t>& out) = 0;

  /// Stage 2: turn bytes into whatever the game needs. Return false on malformed data.
  virtual bool deserialize(const CellId& id, WorldLayer layer, const std::vector<uint8_t>& bytes,
                           CellContent& out) = 0;

  /// Stage 3: create GPU resources. Return false if the upload failed.
  virtual bool upload(const CellId& id, WorldLayer layer, CellContent& content) = 0;

  /// Tear down everything the other three stages built.
  virtual void release(const CellId& id, WorldLayer layer, CellContent& content) = 0;

  // ── Persistence (WM-2.5), optional ──
  //
  // A provider that has no mutable runtime state can ignore both. Both run on the calling thread,
  // never a worker, so they may touch live engine objects.

  /// Capture the difference between the live layer and its authored state into `out`. Called just
  /// before release() on unload. Return false, or leave `out` empty, when the layer is pristine —
  /// an untouched cell should cost no persistence memory.
  virtual bool captureDelta(const CellId& /*id*/, WorldLayer /*layer*/, const CellContent& /*content*/,
                            struct CellDelta& /*out*/) {
    return false;
  }

  /// Reapply a delta captured on a previous unload onto freshly loaded content. Called after
  /// upload(), before the layer is marked resident, so the player never sees the pristine state
  /// flash before its mutations return.
  virtual void applyDelta(const CellId& /*id*/, WorldLayer /*layer*/, CellContent& /*content*/,
                          const struct CellDelta& /*delta*/) {}
};

/// What the scheduler did, for the debug overlay and for tests.
struct StreamingStats {
  uint32_t cellsResident = 0;
  uint32_t cellsLoading = 0;
  uint32_t layersLoaded = 0;

  uint32_t loadsStarted = 0;   ///< this frame
  uint32_t loadsCompleted = 0; ///< this frame
  uint32_t loadsFailed = 0;    ///< cumulative
  uint32_t unloadsIssued = 0;  ///< this frame
  uint32_t cancellations = 0;  ///< cumulative — cells abandoned before they finished loading

  uint32_t budgetRejections = 0; ///< this frame
  uint64_t cpuBytes = 0;
  uint64_t gpuBytes = 0;

  /// Cells inside a load radius that are not resident yet. Zero means streaming has caught up;
  /// a number that keeps climbing means the observer is outrunning the disk.
  uint32_t cellsWanted = 0;
  uint32_t cellsMissing = 0;
};

} // namespace tucano::world
