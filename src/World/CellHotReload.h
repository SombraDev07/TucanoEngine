#pragma once

// WM-9: hot reload of streamed cells.
//
// Watches a world's `cells/` directory and, when a `.tcell` file changes on disk, asks the streamer
// to re-stream that cell in place. That closes the editor loop: save a cell in the editor, and a
// running game shows the edit within a frame or two — no world reload, no lost position, the rest of
// the world untouched.
//
// It is a poll-based watcher on purpose. A cross-platform native file-notification API (inotify /
// ReadDirectoryChangesW) is more code and more failure modes than this needs; the world's cell
// directory is small, and polling it once every few frames costs a directory scan. The cell's
// coordinate is encoded in its filename (see cellFilePath), so a changed file maps straight back to
// a CellId with no index to consult — which is exactly why the on-disk format put it there.

#include "World/CellId.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tucano::world {

class StreamingScheduler;

class CellHotReload {
public:
  /// `worldRoot` is the same path the provider streams from. The scheduler must outlive this.
  CellHotReload(std::string worldRoot, StreamingScheduler& scheduler);

  /// Scans for changes and issues a reload for each cell whose file changed. Returns how many cells
  /// were requested. The FIRST call primes the baseline and requests nothing — it records the world
  /// as it is, so only edits made after the watcher started count. Call it periodically (every few
  /// frames is plenty), on the same thread that drives the scheduler's update().
  uint32_t poll();

  /// Cells requested to reload over this watcher's lifetime — the headline number for a hot-reload
  /// test and a debug overlay.
  uint32_t totalReloads() const { return m_totalReloads; }

private:
  /// Parses `L<level>_<x>_<y>_<z>_<layer>.tcell` back into a CellId. Returns false for anything that
  /// does not match, so stray files in the directory are ignored rather than misread.
  static bool parseCellFile(const std::string& filename, CellId& outId, uint32_t& outLayer);

  std::string m_cellsDir;
  StreamingScheduler& m_scheduler;
  std::unordered_map<std::string, int64_t> m_mtimes; ///< path → last-seen write time (ticks)
  bool m_primed = false;
  uint32_t m_totalReloads = 0;
};

} // namespace tucano::world
