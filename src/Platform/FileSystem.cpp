#include "Platform/FileSystem.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace tucano {

std::vector<uint8_t> readFileBytes(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  const auto size = file.tellg();
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

std::string readFileText(const std::string& path) {
  const auto bytes = readFileBytes(path);
  return std::string(bytes.begin(), bytes.end());
}

bool fileExists(const std::string& path) { return fs::exists(path); }

std::string joinPath(const std::string& a, const std::string& b) {
  return (fs::path(a) / b).string();
}

std::string parentPath(const std::string& path) {
  return fs::path(path).parent_path().string();
}

} // namespace tucano
