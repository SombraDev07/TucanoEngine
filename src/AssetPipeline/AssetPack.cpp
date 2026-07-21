#include "AssetPipeline/AssetPack.h"

#include <cstring>
#include <fstream>

namespace tucano {

// Lightweight store: uncompressed payload with TOC. ZSTD can wrap blob later without API change.
bool AssetPack::create(const std::string& packPath,
                       const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  std::ofstream out(packPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  const uint32_t magic = 0x474B4354; // 'TCKG'
  const uint32_t version = 1;
  const uint32_t count = static_cast<uint32_t>(files.size());
  out.write(reinterpret_cast<const char*>(&magic), 4);
  out.write(reinterpret_cast<const char*>(&version), 4);
  out.write(reinterpret_cast<const char*>(&count), 4);

  uint64_t payloadOffset = 12;
  for (const auto& f : files) {
    payloadOffset += 4 + f.first.size() + 8 + 4 + 4;
  }

  uint64_t cursor = payloadOffset;
  for (const auto& f : files) {
    const uint32_t nameLen = static_cast<uint32_t>(f.first.size());
    const uint32_t size = static_cast<uint32_t>(f.second.size());
    out.write(reinterpret_cast<const char*>(&nameLen), 4);
    out.write(f.first.data(), nameLen);
    out.write(reinterpret_cast<const char*>(&cursor), 8);
    out.write(reinterpret_cast<const char*>(&size), 4);
    out.write(reinterpret_cast<const char*>(&size), 4); // compressed == uncompressed for now
    cursor += size;
  }
  for (const auto& f : files) {
    if (!f.second.empty()) {
      out.write(reinterpret_cast<const char*>(f.second.data()), f.second.size());
    }
  }
  return true;
}

bool AssetPack::open(const std::string& packPath) {
  m_path = packPath;
  m_entries.clear();
  m_blob.clear();
  std::ifstream in(packPath, std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const size_t fileSize = static_cast<size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  m_blob.resize(fileSize);
  in.read(reinterpret_cast<char*>(m_blob.data()), fileSize);
  if (m_blob.size() < 12) {
    return false;
  }
  uint32_t magic = 0, version = 0, count = 0;
  std::memcpy(&magic, m_blob.data(), 4);
  std::memcpy(&version, m_blob.data() + 4, 4);
  std::memcpy(&count, m_blob.data() + 8, 4);
  if (magic != 0x474B4354u || version != 1) {
    return false;
  }
  size_t off = 12;
  for (uint32_t i = 0; i < count; ++i) {
    if (off + 4 > m_blob.size()) {
      return false;
    }
    uint32_t nameLen = 0;
    std::memcpy(&nameLen, m_blob.data() + off, 4);
    off += 4;
    if (off + nameLen + 16 > m_blob.size()) {
      return false;
    }
    PackEntry e;
    e.path.assign(reinterpret_cast<const char*>(m_blob.data() + off), nameLen);
    off += nameLen;
    std::memcpy(&e.offset, m_blob.data() + off, 8);
    off += 8;
    std::memcpy(&e.uncompressedSize, m_blob.data() + off, 4);
    off += 4;
    std::memcpy(&e.compressedSize, m_blob.data() + off, 4);
    off += 4;
    m_entries.push_back(std::move(e));
  }
  return true;
}

bool AssetPack::read(const std::string& path, std::vector<uint8_t>& out) const {
  for (const auto& e : m_entries) {
    if (e.path != path) {
      continue;
    }
    if (e.offset + e.uncompressedSize > m_blob.size()) {
      return false;
    }
    out.assign(m_blob.begin() + static_cast<std::ptrdiff_t>(e.offset),
               m_blob.begin() + static_cast<std::ptrdiff_t>(e.offset + e.uncompressedSize));
    return true;
  }
  return false;
}

} // namespace tucano
