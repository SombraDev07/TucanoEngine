#pragma once

#include "Core/JobSystem.h"
#include "ECS/Components.h"
#include "ECS/EntityManager.h"
#include "ECS/EventManager.h"
#include "ECS/QueryManager.h"
#include "ECS/TemplateManager.h"

#include <array>
#include <initializer_list>
#include <string_view>

namespace tucano::ecs {

// Fachada do ECS completo: EntityManager (archetypes/chunks SoA) + QueryManager (bloom + MT)
// + EventManager (deferred FIFO) + TemplateManager (JSON) + JobSystem compartilhado.
class World {
public:
  World();
  ~World() = default;

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // --- Entidades ---
  Entity create() { return m_entities.create({}); }
  Entity create(std::initializer_list<uint32_t> comps) { return m_entities.create(comps); }
  template <typename... Cs>
  Entity createWith() {
    std::array<uint32_t, sizeof...(Cs)> ids{componentId<Cs>()...};
    return m_entities.create(std::span<const uint32_t>(ids.data(), ids.size()));
  }
  Entity instantiate(std::string_view templateName) {
    return m_templates.instantiate(m_entities, templateName);
  }
  void destroy(Entity e) { m_entities.destroy(e); }
  bool alive(Entity e) const { return m_entities.alive(e); }
  uint32_t liveCount() const { return m_entities.liveCount(); }

  // --- Componentes ---
  template <typename T>
  T* add(Entity e, const T& v = {}) {
    return m_entities.add<T>(e, v);
  }
  template <typename T>
  T* get(Entity e) {
    return m_entities.get<T>(e);
  }
  template <typename T>
  bool has(Entity e) const {
    return m_entities.has(e, componentId<T>());
  }
  template <typename T>
  void remove(Entity e) {
    m_entities.remove(e, componentId<T>());
  }

  // --- Subsistemas ---
  EntityManager& entities() { return m_entities; }
  QueryManager& queries() { return m_queries; }
  EventManager& events() { return m_events; }
  TemplateManager& templates() { return m_templates; }
  core::JobSystem& jobs() { return m_jobs; }

  // Fim do tick: entrega eventos pendentes.
  void tick() { m_events.flush(); }

private:
  EntityManager m_entities;
  QueryManager m_queries{m_entities};
  EventManager m_events;
  TemplateManager m_templates;
  core::JobSystem m_jobs;
};

} // namespace tucano::ecs
