#include "ECS/PhysicsSync.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>


namespace tucano::ecs {

static glm::vec3 toGlm(JPH::RVec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
static glm::quat toGlm(JPH::Quat q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

void syncPhysicsToTransforms(physics::PhysicsWorld& phys, World& world) {
  auto& lockInterface = phys.bodyLockInterface();

  const QueryDesc desc{{"transform"}, {"physics_body"}, {}, {}};
  const QueryId q = world.queries().registerQuery(desc);
  world.queries().performQueryChunks(q, [&](ComponentAccessor& acc) {
    auto* transforms = acc.column<TransformComponent>();
    auto* bodies = acc.column<PhysicsBodyComponent>();
    for (uint32_t i = 0; i < acc.count(); ++i) {
      JPH::BodyLockRead lock(lockInterface, bodies[i].joltBodyId);
      if (!lock.Succeeded()) {
        continue;
      }
      const JPH::Body& body = lock.GetBody();
      // Roll the current state into prev, then read the new state → render interpolation.
      transforms[i].prevPosition = transforms[i].position;
      transforms[i].prevRotation = transforms[i].rotation;
      transforms[i].position = toGlm(body.GetPosition());
      transforms[i].rotation = toGlm(body.GetRotation());
    }
  });
}

void syncTransformsToScene(World& world, tucano::Scene& scene, float alpha) {
  alpha = glm::clamp(alpha, 0.0f, 1.0f);
  const QueryDesc desc{{}, {"transform", "mesh"}, {}, {}};
  const QueryId q = world.queries().registerQuery(desc);
  world.queries().performQueryChunks(q, [&](ComponentAccessor& acc) {
    auto* transforms = acc.column<TransformComponent>();
    auto* renders = acc.column<RenderObjectComponent>();
    for (uint32_t i = 0; i < acc.count(); ++i) {
      const uint32_t idx = renders[i].sceneIndex;
      if (idx >= scene.objects.size()) {
        continue;
      }
      const auto& t = transforms[i];
      // Interpolate between the previous and current fixed-step states for smooth motion.
      const glm::vec3 pos = glm::mix(t.prevPosition, t.position, alpha);
      const glm::quat rot = glm::slerp(t.prevRotation, t.rotation, alpha);
      scene.objects[idx].worldMatrix = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) *
                                       glm::scale(glm::mat4(1.0f), t.scale);
    }
  });
}

} // namespace tucano::ecs
