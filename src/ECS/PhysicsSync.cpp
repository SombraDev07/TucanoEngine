#include "ECS/PhysicsSync.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>

namespace tucano::ecs {

static glm::vec3 toGlm(JPH::RVec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
static glm::quat toGlm(JPH::Quat q)  { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

void syncPhysicsToTransforms(physics::PhysicsWorld& phys, World& world) {
  auto& bodyInterface = phys.bodyInterface();
  auto& lockInterface = phys.bodyLockInterface();

  for (size_t i = 0; i < world.physicsBodies.size(); ++i) {
    Entity e = world.physicsBodies.m_entities[i];
    auto* physComp = world.physicsBodies.get(e);
    if (!physComp) continue;

    auto* transform = world.transforms.get(e);
    if (!transform) continue;

    JPH::BodyLockRead lock(lockInterface, physComp->joltBodyId);
    if (!lock.Succeeded()) continue;

    const JPH::Body& body = lock.GetBody();
    transform->position = toGlm(body.GetPosition());
    transform->rotation = toGlm(body.GetRotation());
  }
}

void syncTransformsToScene(World& world, Scene& scene) {
  for (size_t i = 0; i < world.renderObjects.size(); ++i) {
    Entity e = world.renderObjects.m_entities[i];
    auto* renderComp = world.renderObjects.get(e);
    if (!renderComp || renderComp->sceneIndex >= scene.objects.size()) continue;

    auto* transform = world.transforms.get(e);
    if (!transform) continue;

    auto& obj = scene.objects[renderComp->sceneIndex];
    obj.worldMatrix = glm::translate(glm::mat4(1.0f), transform->position)
                    * glm::mat4_cast(transform->rotation)
                    * glm::scale(glm::mat4(1.0f), transform->scale);
  }
}

} // namespace tucano::ecs
