#pragma once

#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {

// Ref-counted lazy asset cache (Dagor GameResourceFactory pattern, reimplemented).
template <typename T>
class ResourceHandle {
public:
  ResourceHandle() = default;
  explicit ResourceHandle(std::shared_ptr<T> p) : m_ptr(std::move(p)) {}
  T* get() const { return m_ptr.get(); }
  T* operator->() const { return m_ptr.get(); }
  explicit operator bool() const { return static_cast<bool>(m_ptr); }
  long useCount() const { return m_ptr.use_count(); }

private:
  std::shared_ptr<T> m_ptr;
};

class ResourceFactory {
public:
  using MeshLoader = std::function<std::shared_ptr<Mesh>(const std::string&)>;
  using TextureLoader = std::function<std::shared_ptr<Texture>(const std::string&)>;

  void setMeshLoader(MeshLoader loader) { m_meshLoader = std::move(loader); }
  void setTextureLoader(TextureLoader loader) { m_texLoader = std::move(loader); }

  ResourceHandle<Mesh> getMesh(const std::string& name);
  ResourceHandle<Texture> getTexture(const std::string& name);
  void releaseUnused();
  // Hot-reload hook: invalidate cache entry so next get*() reloads.
  void invalidate(const std::string& name);
  size_t meshCount() const { return m_meshes.size(); }
  size_t textureCount() const { return m_textures.size(); }

private:
  MeshLoader m_meshLoader;
  TextureLoader m_texLoader;
  std::unordered_map<std::string, std::weak_ptr<Mesh>> m_meshes;
  std::unordered_map<std::string, std::weak_ptr<Texture>> m_textures;
  mutable std::mutex m_mutex;
};

// Minimal file-watcher poller for hot reload (checks mtime).
class AssetWatcher {
public:
  void watch(const std::string& path);
  // Returns paths whose mtime changed since last poll.
  std::vector<std::string> poll();

private:
  struct Entry {
    std::string path;
    uint64_t mtime = 0;
  };
  std::vector<Entry> m_entries;
};

} // namespace tucano
