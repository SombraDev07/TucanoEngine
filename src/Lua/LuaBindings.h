#pragma once

struct lua_State;

namespace tucano {

namespace ecs { class World; }
namespace physics { class PhysicsWorld; }
namespace rhi { class Device; }
class Renderer;
class Camera;
class Audio;
class Input;
class Scene;

namespace LuaBindings {

void setTimeValues(float dt);

void registerLog(lua_State* L);
void registerTime(lua_State* L);
void registerMath(lua_State* L);
void registerECS(lua_State* L, ecs::World* world);
void registerInput(lua_State* L, Input* input);
void registerPhysics(lua_State* L, physics::PhysicsWorld* phys, Camera* camera);
void registerState(lua_State* L);
void registerTween(lua_State* L);
void registerAudio(lua_State* L, Audio* audio);
void registerAnimation(lua_State* L);
void registerRender2D(lua_State* L, Renderer* renderer);
void registerEvents(lua_State* L);
void registerTrigger(lua_State* L);
void registerPool(lua_State* L);
void registerCamera(lua_State* L, Camera* camera);
void registerSpline(lua_State* L);
void registerNetwork(lua_State* L);
void registerRPC(lua_State* L);
void registerReplication(lua_State* L);
void registerAnimController(lua_State* L);
void registerNavMesh(lua_State* L);
void registerCoroutine(lua_State* L);
void registerBlendSpace(lua_State* L);
void registerAnimLayers(lua_State* L);
void registerAnimEvents(lua_State* L);
void registerScenePrimitives(lua_State* L);
void registerRendererSettings(lua_State* L);
void registerVegetation(lua_State* L);
void registerBiome(lua_State* L);
void registerDensityMap(lua_State* L);
void registerExclusionZones(lua_State* L);
void registerInteraction(lua_State* L);
void registerSeason(lua_State* L);
void registerGrowth(lua_State* L);
void registerWindEvents(lua_State* L);

// Drops GPU resources cached by the bindings (the shared cube mesh). Must run before the device
// they were created on is destroyed — releasing one afterwards dereferences a dangling device
// pointer in ~DX12Buffer. Rebuilt on next use.
void releaseCachedResources();

} // namespace LuaBindings
} // namespace tucano
