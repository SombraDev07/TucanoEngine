#include "World/CellHotReload.h"

#include "World/StreamingScheduler.h"

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace tucano::world {

namespace fs = std::filesystem;

CellHotReload::CellHotReload(std::string worldRoot, StreamingScheduler& scheduler)
    : m_cellsDir(std::move(worldRoot) + "/cells"), m_scheduler(scheduler) {}

bool CellHotReload::parseCellFile(const std::string& filename, CellId& outId, uint32_t& outLayer) {
  // L<level>_<x>_<y>_<z>_<layer>.tcell — signed coordinates, so %d not %u for x/y/z.
  int level = 0, x = 0, y = 0, z = 0, layer = 0;
  if (std::sscanf(filename.c_str(), "L%d_%d_%d_%d_%d.tcell", &level, &x, &y, &z, &layer) != 5) {
    return false;
  }
  outId = CellId{x, y, z, uint32_t(level)};
  outLayer = uint32_t(layer);
  return true;
}

uint32_t CellHotReload::poll() {
  std::error_code ec;
  if (!fs::exists(m_cellsDir, ec)) return 0;

  // Collect this scan's files and their write times. Dedup reload requests per cell: several layer
  // files of one cell changing together should reload the cell once, not four times.
  std::unordered_set<uint64_t> requested;
  uint32_t issued = 0;

  for (const auto& entry : fs::directory_iterator(m_cellsDir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (entry.path().extension() != ".tcell") continue;

    const std::string path = entry.path().string();
    const auto writeTime = fs::last_write_time(entry.path(), ec);
    if (ec) continue;
    const int64_t ticks = int64_t(writeTime.time_since_epoch().count());

    auto it = m_mtimes.find(path);
    const bool changed = (it == m_mtimes.end()) || (it->second != ticks);
    m_mtimes[path] = ticks;

    // On the priming pass we only record; nothing existed "before" to have changed.
    if (!m_primed || !changed) continue;

    CellId id;
    uint32_t layer = 0;
    if (!parseCellFile(entry.path().filename().string(), id, layer)) continue;
    if (requested.insert(id.key()).second) {
      m_scheduler.requestReload(id);
      ++issued;
    }
  }

  m_primed = true;
  m_totalReloads += issued;
  return issued;
}

} // namespace tucano::world
