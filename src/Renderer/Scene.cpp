#include "Renderer/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <utility>

namespace tucano {

glm::mat4 Transform::matrix() const {
  const glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
  const glm::mat4 R = glm::mat4_cast(rotation);
  const glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
  return T * R * S;
}

void Scene::addDirectional(const glm::vec3& dir, const glm::vec3& color, float intensity) {
  Light l;
  l.type = LightType::Directional;
  l.direction = glm::normalize(dir);
  l.color = color;
  l.intensity = intensity;
  lights.push_back(l);
}

void Scene::addPoint(const glm::vec3& pos, const glm::vec3& color, float intensity, float range) {
  Light l;
  l.type = LightType::Point;
  l.position = pos;
  l.color = color;
  l.intensity = intensity;
  l.range = range;
  lights.push_back(l);
}

void Scene::addSpot(const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& color, float intensity, float range,
                    float innerDeg, float outerDeg) {
  Light l;
  l.type = LightType::Spot;
  l.position = pos;
  l.direction = glm::normalize(dir);
  l.color = color;
  l.intensity = intensity;
  l.range = range;
  l.innerCone = glm::cos(glm::radians(innerDeg));
  l.outerCone = glm::cos(glm::radians(outerDeg));
  lights.push_back(l);
}

// ── Object lifetime (C-09) ────────────────────────────────────────────────────

void Scene::syncSlots() const {
  if (m_objectSlots.size() == objects.size()) return;

  if (m_objectSlots.size() < objects.size()) {
    // Appended directly, which most of the engine still does.
    const size_t previous = m_objectSlots.size();
    m_objectSlots.resize(objects.size());
    for (size_t i = previous; i < m_objectSlots.size(); ++i) {
      m_objectSlots[i].generation = m_generationFloor;
    }
    return;
  }

  // Shrunk behind our back: a bare clear() or a leftover erase(). Which index means what is now
  // unknowable, so no old handle is allowed to resolve.
  m_objectSlots.resize(objects.size());
  for (ObjectSlot& slot : m_objectSlots) {
    ++slot.generation;
    slot.alive = true;
  }
  m_freeObjects.clear();
}

RenderObjectHandle Scene::addObject(RenderObject object) {
  syncSlots();

  if (!m_freeObjects.empty()) {
    const uint32_t index = m_freeObjects.back();
    m_freeObjects.pop_back();
    // Whole-object assignment: the previous occupant's materials, name and skinning palette all have
    // to go, and forgetting one is a bug that only shows the second time a slot is reused.
    objects[index] = std::move(object);
    m_objectSlots[index].alive = true;
    return makeRenderObjectHandle(index, m_objectSlots[index].generation);
  }

  const uint32_t index = static_cast<uint32_t>(objects.size());
  objects.push_back(std::move(object));
  m_objectSlots.push_back({m_generationFloor, true});
  return makeRenderObjectHandle(index, m_generationFloor);
}

bool Scene::removeObjectAt(uint32_t index) {
  syncSlots();
  if (index >= objects.size() || !m_objectSlots[index].alive) return false;

  // Cleared rather than left as it was: the mesh and material references have to be dropped here or
  // the slot keeps whole GPU buffers alive for as long as the scene lives. A default RenderObject
  // has no mesh, which is what every draw loop already tests before touching one.
  objects[index] = RenderObject{};
  objects[index].visible = false;

  ++m_objectSlots[index].generation;
  m_objectSlots[index].alive = false;
  m_freeObjects.push_back(index);
  return true;
}

bool Scene::removeObject(RenderObjectHandle handle) {
  return resolve(handle) != nullptr && removeObjectAt(renderObjectIndex(handle));
}

void Scene::clearObjects() {
  syncSlots();
  // The floor is raised past every generation in use, and it is what the rebuilt slots will start
  // at — so no handle from before the reload can resolve against whatever lands in its index.
  // Bumping the slots themselves would achieve nothing here: the table is dropped on the next line.
  for (const ObjectSlot& slot : m_objectSlots) {
    m_generationFloor = std::max(m_generationFloor, slot.generation + 1);
  }
  objects.clear();
  m_objectSlots.clear();
  m_freeObjects.clear();
}

RenderObject* Scene::resolve(RenderObjectHandle handle) {
  return const_cast<RenderObject*>(static_cast<const Scene*>(this)->resolve(handle));
}

const RenderObject* Scene::resolve(RenderObjectHandle handle) const {
  if (handle == kInvalidRenderObject) return nullptr;
  syncSlots();

  const uint32_t index = renderObjectIndex(handle);
  if (index >= objects.size()) return nullptr;
  const ObjectSlot& slot = m_objectSlots[index];
  if (!slot.alive || slot.generation != renderObjectGeneration(handle)) return nullptr;
  return &objects[index];
}

RenderObjectHandle Scene::handleAt(uint32_t index) const {
  syncSlots();
  if (index >= objects.size() || !m_objectSlots[index].alive) return kInvalidRenderObject;
  return makeRenderObjectHandle(index, m_objectSlots[index].generation);
}

bool Scene::objectAlive(uint32_t index) const {
  syncSlots();
  return index < objects.size() && m_objectSlots[index].alive;
}

size_t Scene::liveObjectCount() const {
  syncSlots();
  size_t live = 0;
  for (const ObjectSlot& slot : m_objectSlots) {
    if (slot.alive) ++live;
  }
  return live;
}

} // namespace tucano
