#include "ECS/Components.h"

#include "Core/Json.h"

namespace tucano::ecs {

uint32_t kCompTransform = kInvalidEntity;
uint32_t kCompPhysicsBody = kInvalidEntity;
uint32_t kCompRenderObject = kInvalidEntity;

namespace {

glm::vec3 readVec3(const core::JsonValue& v, const glm::vec3& def) {
  if (v.isArray() && v.arr.size() >= 3) {
    return {v.arr[0].asFloat(def.x), v.arr[1].asFloat(def.y), v.arr[2].asFloat(def.z)};
  }
  return def;
}

void applyTransform(void* dst, const core::JsonValue& props) {
  auto* t = static_cast<TransformComponent*>(dst);
  if (const auto* p = props.find("position")) {
    t->position = readVec3(*p, t->position);
  }
  if (const auto* s = props.find("scale")) {
    if (s->isNumber()) {
      t->scale = glm::vec3(s->asFloat(1.0f));
    } else {
      t->scale = readVec3(*s, t->scale);
    }
  }
  if (const auto* r = props.find("rotationEuler")) {
    const glm::vec3 e = glm::radians(readVec3(*r, glm::vec3(0.0f)));
    t->rotation = glm::quat(e);
  }
  // Seed the interpolation history so the first rendered frame doesn't lerp from the origin.
  t->prevPosition = t->position;
  t->prevRotation = t->rotation;
}

void applyRenderObject(void* dst, const core::JsonValue& props) {
  auto* r = static_cast<RenderObjectComponent*>(dst);
  // Renamed from "sceneIndex" with C-09: the value is a packed handle now, not a position in the
  // array, and a template carrying the old key would have been read as a handle with generation 0 —
  // which is a valid handle to whatever occupies that slot. Better that an outdated template leaves
  // the component invalid, so `spawnMeshObjects` gives the entity its own object.
  if (const auto* h = props.find("sceneHandle")) {
    r->handle = RenderObjectHandle(h->asInt(int(r->handle)));
  }
}

} // namespace

void registerCoreComponents() {
  kCompTransform = registerComponent<TransformComponent>("transform", &applyTransform);
  kCompPhysicsBody = registerComponent<PhysicsBodyComponent>("physics_body");
  kCompRenderObject = registerComponent<RenderObjectComponent>("mesh", &applyRenderObject);
}

} // namespace tucano::ecs
