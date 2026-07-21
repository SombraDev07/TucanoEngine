#include "AssetPipeline/ResourceFactory.h"

#include <filesystem>

namespace tucano {

ResourceHandle<Mesh> ResourceFactory::getMesh(const std::string& name) {
  std::lock_guard lock(m_mutex);
  if (auto it = m_meshes.find(name); it != m_meshes.end()) {
    if (auto locked = it->second.lock()) {
      return ResourceHandle<Mesh>(locked);
    }
  }
  if (!m_meshLoader) {
    return {};
  }
  auto mesh = m_meshLoader(name);
  if (mesh) {
    m_meshes[name] = mesh;
  }
  return ResourceHandle<Mesh>(mesh);
}

ResourceHandle<Texture> ResourceFactory::getTexture(const std::string& name) {
  std::lock_guard lock(m_mutex);
  if (auto it = m_textures.find(name); it != m_textures.end()) {
    if (auto locked = it->second.lock()) {
      return ResourceHandle<Texture>(locked);
    }
  }
  if (!m_texLoader) {
    return {};
  }
  auto tex = m_texLoader(name);
  if (tex) {
    m_textures[name] = tex;
  }
  return ResourceHandle<Texture>(tex);
}

void ResourceFactory::releaseUnused() {
  std::lock_guard lock(m_mutex);
  for (auto it = m_meshes.begin(); it != m_meshes.end();) {
    if (it->second.expired()) {
      it = m_meshes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = m_textures.begin(); it != m_textures.end();) {
    if (it->second.expired()) {
      it = m_textures.erase(it);
    } else {
      ++it;
    }
  }
}

void ResourceFactory::invalidate(const std::string& name) {
  std::lock_guard lock(m_mutex);
  m_meshes.erase(name);
  m_textures.erase(name);
}

void AssetWatcher::watch(const std::string& path) {
  Entry e;
  e.path = path;
  std::error_code ec;
  e.mtime = static_cast<uint64_t>(
      std::filesystem::last_write_time(path, ec).time_since_epoch().count());
  m_entries.push_back(std::move(e));
}

std::vector<std::string> AssetWatcher::poll() {
  std::vector<std::string> changed;
  for (auto& e : m_entries) {
    std::error_code ec;
    const auto mt = std::filesystem::last_write_time(e.path, ec);
    if (ec) {
      continue;
    }
    const uint64_t now = static_cast<uint64_t>(mt.time_since_epoch().count());
    if (now != e.mtime) {
      e.mtime = now;
      changed.push_back(e.path);
    }
  }
  return changed;
}

} // namespace tucano
