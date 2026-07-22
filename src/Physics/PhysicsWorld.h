#pragma once

#include "Physics/PhysicsTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace tucano::physics {

class PhysicsWorld {
public:
  struct Character {
    JPH::Ref<JPH::CharacterVirtual> joltCharacter;
    glm::vec3 velocity{0.0f};
    bool onGround = false;
  };

  PhysicsWorld(uint32_t maxBodies = 4096);
  ~PhysicsWorld();

  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  void step(float deltaTime);

  JPH::BodyID createStaticBox(glm::vec3 halfExtent, glm::vec3 pos, glm::quat rot = {1,0,0,0});
  JPH::BodyID createStaticSphere(float radius, glm::vec3 pos);
  JPH::BodyID createStaticMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, glm::vec3 pos);

  JPH::BodyID createDynamicBox(glm::vec3 halfExtent, glm::vec3 pos, glm::quat rot = {1,0,0,0}, float mass = 1.0f);
  JPH::BodyID createDynamicSphere(float radius, glm::vec3 pos, float mass = 1.0f);

  bool rayCast(glm::vec3 origin, glm::vec3 dir, float maxDist, float& hitDist, glm::vec3& hitNormal) const;

  JPH::BodyInterface& bodyInterface() { return m_bodyInterface; }
  const JPH::BodyInterface& bodyInterface() const { return m_bodyInterface; }
  const JPH::BodyLockInterface& bodyLockInterface() const { return m_system.GetBodyLockInterface(); }

  glm::vec3 getBodyPosition(JPH::BodyID id) const;
  glm::quat getBodyRotation(JPH::BodyID id) const;
  void setBodyTransform(JPH::BodyID id, glm::vec3 pos, glm::quat rot);
  void setLinearVelocity(JPH::BodyID id, glm::vec3 vel);

  Character* createCharacter(glm::vec3 pos, float radius = 0.3f, float height = 0.8f);
  void moveCharacter(Character* c, glm::vec3 wishDir, float dt, float speed = 5.0f);
  void jumpCharacter(Character* c, float impulse = 6.0f);

private:
  BPLayerInterfaceImpl              m_bpLayerInterface;
  ObjectLayerPairFilterImpl         m_objectLayerPairFilter;
  ObjectVsBroadPhaseLayerFilterImpl m_objectVsBPLayerFilter;

  std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
  JPH::PhysicsSystem                        m_system;
  JPH::BodyInterface&                       m_bodyInterface;

  std::vector<Character> m_characters;
};

} // namespace tucano::physics
