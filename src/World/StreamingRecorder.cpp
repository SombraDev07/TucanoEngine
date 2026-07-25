#include "World/StreamingRecorder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace tucano::world {
namespace {

constexpr uint32_t kMagic = 0x54524543; // 'TREC'
constexpr uint32_t kFormatVersion = 1;

template <typename T>
void writePod(std::vector<uint8_t>& buf, const T& v) {
  const auto* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(T));
}

template <typename T>
bool readPod(const uint8_t*& cur, const uint8_t* end, T& v) {
  if (cur + sizeof(T) > end) return false;
  std::memcpy(&v, cur, sizeof(T));
  cur += sizeof(T);
  return true;
}

/// Key for (cell, layer), so the two never collide in the resident-set fold.
uint64_t entryKey(const CellId& id, WorldLayer layer) {
  uint64_t h = id.key();
  h ^= (uint64_t(layer) + 0x9E3779B97F4A7C15ull) + (h << 6) + (h >> 2);
  return h;
}

} // namespace

const char* streamingEventName(StreamingEventType type) {
  switch (type) {
    case StreamingEventType::LoadStarted: return "LoadStarted";
    case StreamingEventType::Loaded: return "Loaded";
    case StreamingEventType::Unloaded: return "Unloaded";
    case StreamingEventType::Failed: return "Failed";
    default: return "?";
  }
}

void StreamingRecorder::record(uint64_t frame, const CellId& cell, WorldLayer layer,
                               StreamingEventType type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  StreamingEvent e{frame, cell, layer, type};

  if (m_ring.size() < m_capacity) {
    m_ring.push_back(e);
    return;
  }
  // Full: overwrite the oldest and count what was lost, so a reader knows the log is partial.
  m_ring[m_head] = e;
  m_head = (m_head + 1) % m_capacity;
  m_wrapped = true;
  ++m_dropped;
}

std::vector<StreamingEvent> StreamingRecorder::events() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_wrapped) return m_ring;

  // Unroll the ring into chronological order.
  std::vector<StreamingEvent> out;
  out.reserve(m_ring.size());
  out.insert(out.end(), m_ring.begin() + int(m_head), m_ring.end());
  out.insert(out.end(), m_ring.begin(), m_ring.begin() + int(m_head));
  return out;
}

size_t StreamingRecorder::eventCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_ring.size();
}

uint64_t StreamingRecorder::droppedEvents() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_dropped;
}

void StreamingRecorder::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_ring.clear();
  m_head = 0;
  m_wrapped = false;
  m_dropped = 0;
}

bool StreamingRecorder::save(const std::string& path) const {
  const std::vector<StreamingEvent> log = events(); // already chronological

  std::vector<uint8_t> buf;
  writePod(buf, kMagic);
  writePod(buf, kFormatVersion);
  writePod(buf, uint64_t(log.size()));
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    writePod(buf, uint64_t(m_dropped));
  }
  for (const StreamingEvent& e : log) {
    writePod(buf, uint64_t(e.frame));
    // Decoded coordinates, not the packed key: survives a change to the key packing.
    writePod(buf, int32_t(e.cell.x));
    writePod(buf, int32_t(e.cell.y));
    writePod(buf, int32_t(e.cell.z));
    writePod(buf, uint32_t(e.cell.level));
    writePod(buf, uint8_t(e.layer));
    writePod(buf, uint8_t(e.type));
  }

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t written = std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  return written == buf.size();
}

bool StreamingRecorder::load(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  const size_t read = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (read != buf.size()) return false;

  const uint8_t* cur = buf.data();
  const uint8_t* end = buf.data() + buf.size();
  uint32_t magic = 0, version = 0;
  uint64_t count = 0, dropped = 0;
  if (!readPod(cur, end, magic) || magic != kMagic) return false;
  if (!readPod(cur, end, version) || version != kFormatVersion) return false;
  if (!readPod(cur, end, count)) return false;
  if (!readPod(cur, end, dropped)) return false;

  // Parse into a temporary so a truncated file cannot half-replace a live log.
  std::vector<StreamingEvent> parsed;
  parsed.reserve(size_t(count));
  for (uint64_t i = 0; i < count; ++i) {
    StreamingEvent e;
    uint8_t layer = 0, type = 0;
    if (!readPod(cur, end, e.frame) || !readPod(cur, end, e.cell.x) ||
        !readPod(cur, end, e.cell.y) || !readPod(cur, end, e.cell.z) ||
        !readPod(cur, end, e.cell.level) || !readPod(cur, end, layer) ||
        !readPod(cur, end, type)) {
      return false;
    }
    if (layer >= kLayerCount || type > uint8_t(StreamingEventType::Failed)) return false;
    e.layer = WorldLayer(layer);
    e.type = StreamingEventType(type);
    parsed.push_back(e);
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_ring = std::move(parsed);
  m_capacity = std::max<size_t>(m_ring.size(), 1);
  m_head = 0;
  m_wrapped = false;
  m_dropped = dropped;
  return true;
}

std::vector<CellId> StreamingRecorder::residentAfter(const std::vector<StreamingEvent>& log) {
  // Fold the log: the last event per (cell, layer) decides whether it ended up resident.
  std::unordered_map<uint64_t, StreamingEvent> last;
  for (const StreamingEvent& e : log) last[entryKey(e.cell, e.layer)] = e;

  // A cell counts as resident if ANY of its layers ended Loaded — layer composition means a cell
  // can legitimately keep Gameplay while Detail has been released.
  std::unordered_map<uint64_t, CellId> resident;
  for (const auto& [key, e] : last) {
    if (e.type == StreamingEventType::Loaded) resident[e.cell.key()] = e.cell;
  }

  std::vector<CellId> out;
  out.reserve(resident.size());
  for (const auto& [key, id] : resident) out.push_back(id);
  // Sorted so the result is order-independent — two runs that discovered cells in a different
  // order still produce an identical vector.
  std::sort(out.begin(), out.end(),
            [](const CellId& a, const CellId& b) { return a.key() < b.key(); });
  return out;
}

bool StreamingRecorder::sameOutcome(const std::vector<StreamingEvent>& a,
                                    const std::vector<StreamingEvent>& b) {
  return residentAfter(a) == residentAfter(b);
}

} // namespace tucano::world
