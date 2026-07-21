#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

std::vector<uint8_t> readFileBytes(const std::string& path);
std::string readFileText(const std::string& path);
bool fileExists(const std::string& path);
std::string joinPath(const std::string& a, const std::string& b);
std::string parentPath(const std::string& path);

} // namespace tucano
