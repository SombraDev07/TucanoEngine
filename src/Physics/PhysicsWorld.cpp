#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include <glm/gtc/type_ptr.hpp>

#include <mutex>

namespace tucano::physics {

static JPH::RVec3 toJolt(glm::vec3 v) { return {v.x, v.y, v.z}; }
static JPH::Quat  toJolt(glm::quat q) { return {q.x, q.y, q.z, q.w}; }
static glm::vec3  toGlm(JPH::RVec3 v)  { return {v.GetX(), v.GetY(), v.GetZ()}; }
static glm::quat  toGlm(JPH::Quat q)   { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

static std::once_flag s_joltInitFlag;

static void ensureJoltInit() {
  std::call_once(s_joltInitFlag, [] {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    // Registers all shape/collision/constraint types with the Factory. Without this the solver
    // dereferences unregistered types when the first contact constraint is created → crash.
    JPH::RegisterTypes();
  });
}

PhysicsWorld::PhysicsWorld(uint32_t maxBodies)
  : m_system()
  , m_bodyInterface(m_system.GetBodyInterface())
{
  ensureJoltInit();

  JPH::TempAllocatorImpl tempAllocator(32 * 1024 * 1024);
  // maxBarriers must match Jolt's recommended default (8); the previous value (1) could corrupt
  // memory once the solver forms simulation islands.
  m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, -1);

  constexpr uint32_t cMaxPairs    = 65536;
  constexpr uint32_t cMaxContacts = 65536;

  m_system.Init(maxBodies, 0, cMaxPairs, cMaxContacts,
                m_bpLayerInterface, m_objectVsBPLayerFilter, m_objectLayerPairFilter);
}

PhysicsWorld::~PhysicsWorld() {
  m_system.OptimizeBroadPhase();
}

void PhysicsWorld::step(float deltaTime) {
  constexpr int cCollisionSteps = 1;

  // Optimize the broadphase once after the initial bodies are in (Jolt HelloWorld pattern).
  if (!m_broadPhaseOptimized) {
    m_system.OptimizeBroadPhase();
    m_broadPhaseOptimized = true;
  }

  JPH::TempAllocatorImpl tempAllocator(32 * 1024 * 1024);
  m_system.Update(deltaTime, cCollisionSteps, &tempAllocator, m_jobSystem.get());

  JPH::Vec3 gravity(0.0f, -9.81f, 0.0f);
  for (auto& c : m_characters) {
    c.joltCharacter->Update(deltaTime, gravity,
      JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{}, JPH::BodyFilter{}, JPH::ShapeFilter{},
      tempAllocator);
    c.onGround = c.joltCharacter->GetGroundState() != JPH::CharacterBase::EGroundState::InAir;
  }
}

JPH::BodyID PhysicsWorld::createStaticBox(glm::vec3 halfExtent, glm::vec3 pos, glm::quat rot) {
  JPH::BoxShapeSettings shapeSettings(toJolt(halfExtent));
  JPH::ShapeRefC shape = shapeSettings.Create().Get();

  JPH::BodyCreationSettings bodySettings(shape, toJolt(pos), toJolt(rot),
                                          JPH::EMotionType::Static, Layers::STATIC);
  JPH::Body* body = m_bodyInterface.CreateBody(bodySettings);
  m_bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}

JPH::BodyID PhysicsWorld::createStaticSphere(float radius, glm::vec3 pos) {
  JPH::SphereShapeSettings shapeSettings(radius);
  JPH::ShapeRefC shape = shapeSettings.Create().Get();

  JPH::BodyCreationSettings bodySettings(shape, toJolt(pos), JPH::Quat::sIdentity(),
                                          JPH::EMotionType::Static, Layers::STATIC);
  JPH::Body* body = m_bodyInterface.CreateBody(bodySettings);
  m_bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}

JPH::BodyID PhysicsWorld::createStaticMesh(const std::vector<glm::vec3>& vertices,
                                            const std::vector<uint32_t>& indices,
                                            glm::vec3 pos) {
  JPH::VertexList joltVerts;
  joltVerts.reserve(vertices.size());
  for (auto& v : vertices) joltVerts.push_back(JPH::Float3(v.x, v.y, v.z));

  JPH::IndexedTriangleList joltTris;
  joltTris.reserve(indices.size() / 3);
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    joltTris.push_back(JPH::IndexedTriangle(indices[i], indices[i+1], indices[i+2], 0));
  }

  JPH::MeshShapeSettings shapeSettings(joltVerts, joltTris);
  JPH::ShapeRefC shape = shapeSettings.Create().Get();

  JPH::BodyCreationSettings bodySettings(shape, toJolt(pos), JPH::Quat::sIdentity(),
                                          JPH::EMotionType::Static, Layers::STATIC);
  JPH::Body* body = m_bodyInterface.CreateBody(bodySettings);
  m_bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}

JPH::BodyID PhysicsWorld::createDynamicBox(glm::vec3 halfExtent, glm::vec3 pos, glm::quat rot, float mass) {
  JPH::BoxShapeSettings shapeSettings(toJolt(halfExtent));
  JPH::ShapeRefC shape = shapeSettings.Create().Get();

  JPH::BodyCreationSettings bodySettings(shape, toJolt(pos), toJolt(rot),
                                          JPH::EMotionType::Dynamic, Layers::DYNAMIC);
  bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
  bodySettings.mMassPropertiesOverride.mMass = mass;

  JPH::Body* body = m_bodyInterface.CreateBody(bodySettings);
  m_bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
  return body->GetID();
}

JPH::BodyID PhysicsWorld::createDynamicSphere(float radius, glm::vec3 pos, float mass) {
  JPH::SphereShapeSettings shapeSettings(radius);
  JPH::ShapeRefC shape = shapeSettings.Create().Get();

  JPH::BodyCreationSettings bodySettings(shape, toJolt(pos), JPH::Quat::sIdentity(),
                                          JPH::EMotionType::Dynamic, Layers::DYNAMIC);
  bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
  bodySettings.mMassPropertiesOverride.mMass = mass;

  JPH::Body* body = m_bodyInterface.CreateBody(bodySettings);
  m_bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
  return body->GetID();
}

bool PhysicsWorld::rayCast(glm::vec3 origin, glm::vec3 dir, float maxDist,
                            float& hitDist, glm::vec3& hitNormal) const {
  JPH::RRayCast ray{toJolt(origin), JPH::Vec3(dir.x, dir.y, dir.z).Normalized() * maxDist};
  JPH::RayCastResult result;

  const JPH::NarrowPhaseQuery& npQuery = m_system.GetNarrowPhaseQuery();
  if (!npQuery.CastRay(ray, result)) return false;

  hitDist   = ray.GetPointOnRay(result.mFraction).Length();
  hitNormal = glm::vec3(0, 1, 0); // could get from body face but not exposed in simple CastRay
  return true;
}

glm::vec3 PhysicsWorld::getBodyPosition(JPH::BodyID id) const {
  JPH::BodyLockRead lock(m_system.GetBodyLockInterface(), id);
  if (!lock.Succeeded()) return {0,0,0};
  return toGlm(lock.GetBody().GetPosition());
}

glm::quat PhysicsWorld::getBodyRotation(JPH::BodyID id) const {
  JPH::BodyLockRead lock(m_system.GetBodyLockInterface(), id);
  if (!lock.Succeeded()) return {1,0,0,0};
  return toGlm(lock.GetBody().GetRotation());
}

void PhysicsWorld::setBodyTransform(JPH::BodyID id, glm::vec3 pos, glm::quat rot) {
  m_bodyInterface.SetPositionAndRotation(id, toJolt(pos), toJolt(rot),
                                          JPH::EActivation::Activate);
}

void PhysicsWorld::setLinearVelocity(JPH::BodyID id, glm::vec3 vel) {
  m_bodyInterface.SetLinearVelocity(id, JPH::Vec3(vel.x, vel.y, vel.z));
}

PhysicsWorld::Character* PhysicsWorld::createCharacter(glm::vec3 pos, float radius, float height) {
  JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
  settings->mShape = new JPH::CapsuleShape(height * 0.5f, radius);
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius * 1.5f);

  Character c;
  c.joltCharacter = new JPH::CharacterVirtual(
    settings, toJolt(pos), JPH::Quat::sIdentity(), &m_system);
  c.velocity = {0,0,0};
  c.onGround = false;

  m_characters.push_back(std::move(c));
  return &m_characters.back();
}

void PhysicsWorld::moveCharacter(Character* c, glm::vec3 wishDir, float dt, float speed) {
  if (!c || !c->joltCharacter) return;

  glm::vec3 horzDir = glm::length(wishDir) > 0.001f ? glm::normalize(wishDir) * speed : glm::vec3{0};

  JPH::Vec3 desiredVel(horzDir.x, c->velocity.y, horzDir.z);
  c->velocity = glm::vec3(
    c->velocity.x + (desiredVel.GetX() - c->velocity.x) * dt * 10.0f,
    desiredVel.GetY(),
    c->velocity.z + (desiredVel.GetZ() - c->velocity.z) * dt * 10.0f);

  if (!c->onGround) {
    c->velocity.y -= 9.81f * dt;
  }

  // Only set the desired velocity here — the single CharacterVirtual::Update happens in step().
  // (Updating both here and in step() integrated the character twice per frame → visible jitter.)
  JPH::Vec3 joltVel(c->velocity.x, c->velocity.y, c->velocity.z);
  c->joltCharacter->SetLinearVelocity(joltVel);
}

void PhysicsWorld::jumpCharacter(Character* c, float impulse) {
  if (!c || !c->onGround) return;
  c->velocity.y = impulse;
  c->onGround = false;
}

} // namespace tucano::physics
