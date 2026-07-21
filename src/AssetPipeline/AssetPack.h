#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

// Shipping pack: single .tcpkg (ZSTD-ready container). Runtime mounts as VFS-like index.
struct PackEntry {
  std::string path;
  uint64_t offset = 0;
  uint32_t uncompressedSize = 0;
  uint32_t compressedSize = 0;
};

class AssetPack {
public:
  bool create(const std::string& packPath, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);
  bool open(const std::string& packPath);
  bool read(const std::string& path, std::vector<uint8_t>& out) const;
  const std::vector<PackEntry>& entries() const { return m_entries; }

private:
  std::string m_path;
  std::vector<PackEntry> m_entries;
  std::vector<uint8_t> m_blob;
};

} // namespace tucano
