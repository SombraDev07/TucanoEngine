#pragma once

#include "ECS/Components.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace tucano::ecs {

using Entity = uint32_t;
constexpr Entity kInvalidEntity = std::numeric_limits<Entity>::max();

template<typename T>
class ComponentPool {
public:
  T* get(Entity e) {
    auto it = m_map.find(e);
    return it != m_map.end() ? &m_data[it->second] : nullptr;
  }

  const T* get(Entity e) const {
    auto it = m_map.find(e);
    return it != m_map.end() ? &m_data[it->second] : nullptr;
  }

  T* add(Entity e, T&& val) {
    auto it = m_map.find(e);
    if (it != m_map.end()) {
      m_data[it->second] = std::move(val);
      return &m_data[it->second];
    }
    m_entities.push_back(e);
    m_data.push_back(std::move(val));
    m_map[e] = static_cast<uint32_t>(m_data.size() - 1);
    return &m_data.back();
  }

  void remove(Entity e) {
    auto it = m_map.find(e);
    if (it == m_map.end()) return;
    size_t idx = it->second;
    size_t last = m_data.size() - 1;
    if (idx != last) {
      m_entities[idx] = m_entities[last];
      m_data[idx] = std::move(m_data[last]);
      m_map[m_entities[idx]] = static_cast<uint32_t>(idx);
    }
    m_entities.pop_back();
    m_data.pop_back();
    m_map.erase(e);
  }

  bool has(Entity e) const { return m_map.find(e) != m_map.end(); }
  size_t size() const { return m_data.size(); }

  std::vector<Entity> m_entities;
  std::vector<T> m_data;

private:
  std::unordered_map<Entity, uint32_t> m_map;
};

class World {
public:
  static constexpr size_t kMaxEntities = 65536;

  World()  = default;
  ~World() = default;

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  Entity create();
  void   destroy(Entity e);
  bool   alive(Entity e) const;

  ComponentPool<TransformComponent>     transforms;
  ComponentPool<PhysicsBodyComponent>   physicsBodies;
  ComponentPool<RenderObjectComponent>  renderObjects;

private:
  std::vector<bool> m_alive = std::vector<bool>(kMaxEntities, false);
  Entity m_nextEntity = 0;
};

} // namespace tucano::ecs
