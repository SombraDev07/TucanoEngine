#include "ECS/World.h"

namespace tucano::ecs {

Entity World::create() {
  for (size_t i = 0; i < kMaxEntities; ++i) {
    Entity e = m_nextEntity;
    m_nextEntity = (m_nextEntity + 1) % kMaxEntities;
    if (!m_alive[e]) {
      m_alive[e] = true;
      return e;
    }
  }
  throw std::runtime_error("World: max entities reached");
}

void World::destroy(Entity e) {
  if (!m_alive[e]) return;
  transforms.remove(e);
  physicsBodies.remove(e);
  renderObjects.remove(e);
  m_alive[e] = false;
}

bool World::alive(Entity e) const {
  return e < kMaxEntities && m_alive[e];
}

} // namespace tucano::ecs
