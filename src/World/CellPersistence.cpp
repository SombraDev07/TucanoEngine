#include "World/CellPersistence.h"

#include <cstdio>
#include <cstring>

namespace tucano::world {
namespace {

/// Magic + version at the head of a saved delta file, so a format change is rejected rather than
/// misread as data.
constexpr uint32_t kMagic = 0x544C4443; // 'TLDC' — Tucano cell delta cache
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

} // namespace

bool CellPersistenceStore::has(const CellId& id, WorldLayer layer) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_entries.find(entryKey(id, layer));
  return it != m_entries.end() && !it->second.delta.empty();
}

bool CellPersistenceStore::get(const CellId& id, WorldLayer layer, CellDelta& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_entries.find(entryKey(id, layer));
  if (it == m_entries.end() || it->second.delta.empty()) return false;
  out = it->second.delta; // copy under the lock
  return true;
}

void CellPersistenceStore::put(const CellId& id, WorldLayer layer, CellDelta delta) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const uint64_t key = entryKey(id, layer);
  if (delta.empty()) {
    // A cell reverted to its authored state should stop costing memory.
    m_entries.erase(key);
    return;
  }
  auto& entry = m_entries[key];
  entry.id = id;
  entry.layer = layer;
  // Preserve a monotonic version even when the caller did not set one, so replay ordering holds.
  const uint64_t nextVersion = entry.delta.version + 1;
  entry.delta = std::move(delta);
  if (entry.delta.version == 0) entry.delta.version = nextVersion;
}

void CellPersistenceStore::clear(const CellId& id, WorldLayer layer) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_entries.erase(entryKey(id, layer));
}

void CellPersistenceStore::clearAll() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_entries.clear();
}

size_t CellPersistenceStore::deltaCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_entries.size();
}

uint64_t CellPersistenceStore::totalBytes() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  uint64_t sum = 0;
  for (const auto& [key, entry] : m_entries) sum += entry.delta.bytes.size();
  return sum;
}

bool CellPersistenceStore::save(const std::string& path) const {
  std::vector<uint8_t> buf;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    writePod(buf, kMagic);
    writePod(buf, kFormatVersion);
    writePod(buf, uint64_t(m_entries.size()));
    for (const auto& [key, entry] : m_entries) {
      // Store the decoded coordinate + level, not the packed key: it survives a change to the key
      // packing, which the raw key would not.
      writePod(buf, int32_t(entry.id.x));
      writePod(buf, int32_t(entry.id.y));
      writePod(buf, int32_t(entry.id.z));
      writePod(buf, uint32_t(entry.id.level));
      writePod(buf, uint8_t(entry.layer));
      writePod(buf, uint64_t(entry.delta.version));
      writePod(buf, uint64_t(entry.delta.bytes.size()));
      buf.insert(buf.end(), entry.delta.bytes.begin(), entry.delta.bytes.end());
    }
  }

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t written = std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  return written == buf.size();
}

bool CellPersistenceStore::load(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  // static_cast, not size_t(size): the latter is the most vexing parse — the compiler reads it as
  // a function declaration named buf rather than a vector.
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  const size_t read = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (read != buf.size()) return false;

  const uint8_t* cur = buf.data();
  const uint8_t* end = buf.data() + buf.size();

  uint32_t magic = 0, version = 0;
  uint64_t count = 0;
  if (!readPod(cur, end, magic) || magic != kMagic) return false;
  if (!readPod(cur, end, version) || version != kFormatVersion) return false;
  if (!readPod(cur, end, count)) return false;

  // Parse into a temporary so a truncated file cannot half-replace the live store.
  std::unordered_map<uint64_t, Entry> parsed;
  parsed.reserve(size_t(count));
  for (uint64_t i = 0; i < count; ++i) {
    Entry e;
    uint8_t layer = 0;
    uint64_t byteLen = 0;
    if (!readPod(cur, end, e.id.x) || !readPod(cur, end, e.id.y) || !readPod(cur, end, e.id.z) ||
        !readPod(cur, end, e.id.level) || !readPod(cur, end, layer) ||
        !readPod(cur, end, e.delta.version) || !readPod(cur, end, byteLen)) {
      return false;
    }
    if (layer >= kLayerCount) return false;
    if (cur + byteLen > end) return false;
    e.layer = WorldLayer(layer);
    e.delta.bytes.assign(cur, cur + byteLen);
    cur += byteLen;
    parsed.emplace(entryKey(e.id, e.layer), std::move(e));
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_entries = std::move(parsed);
  return true;
}

} // namespace tucano::world
