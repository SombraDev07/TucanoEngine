#pragma once

// WM-2.5: cell persistence.
//
// Streaming that silently discards what the player did is a bug, not a limitation. Cross a cell
// boundary, come back, and the door you opened is shut, the crate you smashed is whole, the guard
// you killed is patrolling again. The World Partition in UE5 solves this the same way this does:
// the authored cell data is immutable, and runtime changes are recorded as a per-cell DELTA that
// is captured on unload and replayed on reload.
//
// The World module stays ignorant of what a mutation actually is. It moves opaque delta blobs; the
// game's CellDataProvider is what knows how to diff a live cell against its authored state and how
// to reapply that diff. So this store is pure bookkeeping — a map from (cell, layer) to bytes,
// with a session save/load so the deltas survive a quit as a save-game would.

#include "World/CellId.h"
#include "World/WorldGrid.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::world {

/// Recorded runtime mutations for one (cell, layer). Opaque to the World module.
struct CellDelta {
  std::vector<uint8_t> bytes;
  /// Bumped each time the delta is recaptured. Lets a replay or a networked host detect which of
  /// two captures of the same cell is newer without comparing the bytes.
  uint64_t version = 0;

  bool empty() const { return bytes.empty(); }
};

/// Holds deltas between a cell's unload and its next load, and across a session via save/load.
///
/// Thread-safety: capture happens on the main thread (inside the scheduler's update), but a future
/// background save must be able to read the store, so it is internally locked.
class CellPersistenceStore {
public:
  /// True if a non-empty delta exists for this layer.
  bool has(const CellId& id, WorldLayer layer) const;

  /// Copies the delta out, or returns false if there is none. A copy, not a pointer, so a
  /// concurrent capture cannot invalidate it under the caller.
  bool get(const CellId& id, WorldLayer layer, CellDelta& out) const;

  /// Stores (or replaces) a delta. An empty delta erases any existing one — a cell reverted to its
  /// authored state should stop costing memory.
  void put(const CellId& id, WorldLayer layer, CellDelta delta);

  /// Drops the delta for one layer, e.g. after the game explicitly resets a cell.
  void clear(const CellId& id, WorldLayer layer);

  void clearAll();

  size_t deltaCount() const;
  uint64_t totalBytes() const;

  /// Serializes every delta to `path`. Format is a small self-describing binary blob; see the .cpp.
  bool save(const std::string& path) const;

  /// Replaces the store's contents with the deltas from `path`. Returns false on a missing or
  /// malformed file, leaving the store unchanged.
  bool load(const std::string& path);

private:
  /// Key combines the cell key and the layer so one map covers every (cell, layer) pair.
  static uint64_t entryKey(const CellId& id, WorldLayer layer) {
    // The cell key uses the top 4 bits for level and 60 for morton, leaving no room, so fold the
    // layer into a separate hash mix rather than shifting it into the key.
    uint64_t h = id.key();
    h ^= (uint64_t(layer) + 0x9E3779B97F4A7C15ull) + (h << 6) + (h >> 2);
    return h;
  }

  struct Entry {
    CellId id;
    WorldLayer layer;
    CellDelta delta;
  };

  mutable std::mutex m_mutex;
  std::unordered_map<uint64_t, Entry> m_entries;
};

} // namespace tucano::world
