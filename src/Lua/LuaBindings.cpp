#include "Lua/LuaBindings.h"

#include "ECS/World.h"
#include "ECS/Components.h"
#include "ECS/ComponentTypes.h"
#include "ECS/TemplateManager.h"
#include "ECS/QueryManager.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/Camera.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene.h"
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Audio/AudioSource.h"
#include "Audio/SoundEvents.h"
#include "Core/StateStorage.h"
#include "Core/Tween.h"
#include "Core/EventBus.h"
#include "Core/ObjectPool.h"
#include "Core/Spline.h"
#include "Platform/Input.h"
#include "Input/InputConfiguration.h"
#include "Input/InputFwd.h"
#include "Input/VirtualInput.h"
#include "Animation/AnimationClip.h"
#include "Animation/AnimationController.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimationLayers.h"
#include "Animation/AnimationEvents.h"
#include "Physics/TriggerVolume.h"
#include "Physics/NavMesh.h"
#include "Renderer/CameraShake.h"
#include "Network/NetworkManager.h"
#include "Network/ReplicationSystem.h"
#include "Network/RPCSystem.h"
#include "Core/Coroutine.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace tucano::LuaBindings {

using Clock = std::chrono::steady_clock;

static Clock::time_point g_startTime;
static float g_deltaTime = 0.0f;
static float g_elapsedTime = 0.0f;

void setTimeValues(float dt) {
	g_deltaTime = dt;
	g_elapsedTime += dt;
}

// ── Component metadata (name → converters) ────────────

using CompToLua = void(*)(lua_State* L, const void* data);
using LuaToComp = void(*)(lua_State* L, int idx, void* data);

struct CompMeta {
	const char* name;
	uint32_t id;
	uint32_t size;
	CompToLua toLua;
	LuaToComp fromLua;
};

static std::vector<CompMeta> g_compMetas;

static void registerCompMeta(const char* name, uint32_t id, uint32_t size,
                             CompToLua toLua, LuaToComp fromLua) {
	g_compMetas.push_back({name, id, size, toLua, fromLua});
}

static const CompMeta* findCompMeta(uint32_t id) {
	for (auto& m : g_compMetas)
		if (m.id == id) return &m;
	return nullptr;
}

// ── helpers ───────────────────────────────────────────

static void pushVec3(lua_State* L, const glm::vec3& v) {
	lua_newtable(L);
	lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
	lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
	lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
}

static glm::vec3 toVec3(lua_State* L, int idx) {
	glm::vec3 v{0,0,0};
	if (lua_istable(L, idx)) {
		lua_getfield(L, idx, "x"); v.x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, idx, "y"); v.y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, idx, "z"); v.z = (float)lua_tonumber(L, -1); lua_pop(L, 1);
	}
	return v;
}

static void pushQuat(lua_State* L, const glm::quat& q) {
	lua_newtable(L);
	lua_pushnumber(L, q.x); lua_setfield(L, -2, "x");
	lua_pushnumber(L, q.y); lua_setfield(L, -2, "y");
	lua_pushnumber(L, q.z); lua_setfield(L, -2, "z");
	lua_pushnumber(L, q.w); lua_setfield(L, -2, "w");
}

static glm::quat toQuat(lua_State* L, int idx) {
	glm::quat q{1,0,0,0};
	if (lua_istable(L, idx)) {
		lua_getfield(L, idx, "x"); q.x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, idx, "y"); q.y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, idx, "z"); q.z = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, idx, "w"); q.w = (float)lua_tonumber(L, -1); lua_pop(L, 1);
	} else if (lua_isnumber(L, idx)) {
		q.w = (float)lua_tonumber(L, idx);
	}
	return q;
}

static ecs::World* getWorld(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_world");
	auto* w = static_cast<ecs::World*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return w;
}

static physics::PhysicsWorld* getPhysics(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_physics");
	auto* p = static_cast<physics::PhysicsWorld*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return p;
}

static Camera* getCamera(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_camera");
	auto* c = static_cast<Camera*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return c;
}

static Input* getInput(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_input");
	auto* i = static_cast<Input*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return i;
}

static Audio* getAudio(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_audio");
	auto* a = static_cast<Audio*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return a;
}

// ── Component converters ──────────────────────────────

static void transformToLua(lua_State* L, const void* data) {
	auto* t = static_cast<const ecs::TransformComponent*>(data);
	pushVec3(L, t->position); lua_setfield(L, -2, "position");
	pushQuat(L, t->rotation);  lua_setfield(L, -2, "rotation");
	pushVec3(L, t->scale);     lua_setfield(L, -2, "scale");
}

static void transformFromLua(lua_State* L, int idx, void* data) {
	auto* t = static_cast<ecs::TransformComponent*>(data);
	lua_getfield(L, idx, "position"); t->position = toVec3(L, -1); lua_pop(L, 1);
	lua_getfield(L, idx, "rotation"); t->rotation = toQuat(L, -1); lua_pop(L, 1);
	lua_getfield(L, idx, "scale");    t->scale = toVec3(L, -1);    lua_pop(L, 1);
}

// ── registerLog ───────────────────────────────────────

static int lua_log_print(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	std::cout << "[Lua] " << msg << std::endl;
	return 0;
}

static int lua_log_warn(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	std::cerr << "[Lua] WARN: " << msg << std::endl;
	return 0;
}

static int lua_log_error(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	std::cerr << "[Lua] ERROR: " << msg << std::endl;
	return 0;
}

void registerLog(lua_State* L) {
	lua_newtable(L);
	lua_pushcfunction(L, lua_log_print); lua_setfield(L, -2, "print");
	lua_pushcfunction(L, lua_log_warn);  lua_setfield(L, -2, "warn");
	lua_pushcfunction(L, lua_log_error); lua_setfield(L, -2, "error");
	lua_setglobal(L, "log");
}

// ── registerTime ──────────────────────────────────────

static int lua_time_delta(lua_State* L) {
	lua_pushnumber(L, g_deltaTime);
	return 1;
}

static int lua_time_elapsed(lua_State* L) {
	lua_pushnumber(L, g_elapsedTime);
	return 1;
}

void registerTime(lua_State* L) {
	lua_newtable(L);
	lua_pushcfunction(L, lua_time_delta);   lua_setfield(L, -2, "delta");
	lua_pushcfunction(L, lua_time_elapsed); lua_setfield(L, -2, "elapsed");
	lua_setglobal(L, "time");
}

// ── registerMath ──────────────────────────────────────

static int lua_vec3_new(lua_State* L) {
	float x = (float)luaL_optnumber(L, 1, 0);
	float y = (float)luaL_optnumber(L, 2, 0);
	float z = (float)luaL_optnumber(L, 3, 0);
	pushVec3(L, {x, y, z});
	return 1;
}

static int lua_vec3_add(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2);
	pushVec3(L, a + b);
	return 1;
}

static int lua_vec3_sub(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2);
	pushVec3(L, a - b);
	return 1;
}

static int lua_vec3_mul(lua_State* L) {
	auto a = toVec3(L, 1);
	if (lua_istable(L, 2)) { pushVec3(L, a * toVec3(L, 2)); }
	else { float s = (float)luaL_checknumber(L, 2); pushVec3(L, a * s); }
	return 1;
}

static int lua_vec3_length(lua_State* L) {
	auto v = toVec3(L, 1);
	lua_pushnumber(L, glm::length(v));
	return 1;
}

static int lua_vec3_normalize(lua_State* L) {
	auto v = toVec3(L, 1);
	pushVec3(L, glm::normalize(v));
	return 1;
}

static int lua_vec3_dot(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2);
	lua_pushnumber(L, glm::dot(a, b));
	return 1;
}

static int lua_vec3_cross(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2);
	pushVec3(L, glm::cross(a, b));
	return 1;
}

static int lua_vec3_lerp(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2); float t = (float)luaL_checknumber(L, 3);
	pushVec3(L, glm::mix(a, b, t));
	return 1;
}

static int lua_vec3_distance(lua_State* L) {
	auto a = toVec3(L, 1); auto b = toVec3(L, 2);
	lua_pushnumber(L, glm::distance(a, b));
	return 1;
}

static int lua_quat_new(lua_State* L) {
	float x = (float)luaL_optnumber(L, 1, 0);
	float y = (float)luaL_optnumber(L, 2, 0);
	float z = (float)luaL_optnumber(L, 3, 0);
	float w = (float)luaL_optnumber(L, 4, 1);
	pushQuat(L, {w, x, y, z});
	return 1;
}

static int lua_quat_from_euler(lua_State* L) {
	float pitch = (float)luaL_checknumber(L, 1);
	float yaw   = (float)luaL_checknumber(L, 2);
	float roll  = (float)luaL_optnumber(L, 3, 0);
	pushQuat(L, glm::quat(glm::vec3(pitch, yaw, roll)));
	return 1;
}

static int lua_quat_mul(lua_State* L) {
	auto a = toQuat(L, 1); auto b = toQuat(L, 2);
	pushQuat(L, a * b);
	return 1;
}

static int lua_quat_slerp(lua_State* L) {
	auto a = toQuat(L, 1); auto b = toQuat(L, 2); float t = (float)luaL_checknumber(L, 3);
	pushQuat(L, glm::slerp(a, b, t));
	return 1;
}

void registerMath(lua_State* L) {
	lua_newtable(L);
	lua_pushcfunction(L, lua_vec3_new);       lua_setfield(L, -2, "new");
	lua_pushcfunction(L, lua_vec3_add);       lua_setfield(L, -2, "add");
	lua_pushcfunction(L, lua_vec3_sub);       lua_setfield(L, -2, "sub");
	lua_pushcfunction(L, lua_vec3_mul);       lua_setfield(L, -2, "mul");
	lua_pushcfunction(L, lua_vec3_length);    lua_setfield(L, -2, "length");
	lua_pushcfunction(L, lua_vec3_normalize); lua_setfield(L, -2, "normalize");
	lua_pushcfunction(L, lua_vec3_dot);       lua_setfield(L, -2, "dot");
	lua_pushcfunction(L, lua_vec3_cross);     lua_setfield(L, -2, "cross");
	lua_pushcfunction(L, lua_vec3_lerp);      lua_setfield(L, -2, "lerp");
	lua_pushcfunction(L, lua_vec3_distance);  lua_setfield(L, -2, "distance");
	lua_setglobal(L, "vec3");

	lua_newtable(L);
	lua_pushcfunction(L, lua_quat_new);       lua_setfield(L, -2, "new");
	lua_pushcfunction(L, lua_quat_from_euler);lua_setfield(L, -2, "from_euler");
	lua_pushcfunction(L, lua_quat_mul);       lua_setfield(L, -2, "mul");
	lua_pushcfunction(L, lua_quat_slerp);     lua_setfield(L, -2, "slerp");
	lua_setglobal(L, "quat");
}

// ── registerECS ───────────────────────────────────────

static int lua_ecs_create(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) { lua_pushinteger(L, 0); return 1; }

	std::vector<uint32_t> comps;
	if (lua_istable(L, 1)) {
		lua_pushnil(L);
		while (lua_next(L, 1)) {
			const char* name = lua_tostring(L, -1);
			if (name) {
				uint32_t id = ecs::ComponentRegistry::instance().find(name);
				if (id != ecs::kInvalidEntity) comps.push_back(id);
			}
			lua_pop(L, 1);
		}
	}

	auto e = world->entities().create(std::span<const uint32_t>(comps.data(), comps.size()));
	lua_pushinteger(L, e);
	return 1;
}

static int lua_ecs_destroy(lua_State* L) {
	auto* world = getWorld(L);
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	if (world) world->destroy(e);
	return 0;
}

static int lua_ecs_alive(lua_State* L) {
	auto* world = getWorld(L);
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	lua_pushboolean(L, world ? world->alive(e) : false);
	return 1;
}

static int lua_ecs_has(lua_State* L) {
	auto* world = getWorld(L);
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	const char* name = luaL_checkstring(L, 2);
	uint32_t id = ecs::ComponentRegistry::instance().find(name);
	lua_pushboolean(L, world ? world->entities().has(e, id) : false);
	return 1;
}

static int lua_ecs_get(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) { lua_pushnil(L); return 1; }

	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	const char* name = luaL_checkstring(L, 2);
	uint32_t id = ecs::ComponentRegistry::instance().find(name);
	if (id == ecs::kInvalidEntity) { lua_pushnil(L); return 1; }

	auto* meta = findCompMeta(id);
	if (!meta || !meta->toLua) { lua_pushnil(L); return 1; }

	void* data = world->entities().get(e, id);
	if (!data) { lua_pushnil(L); return 1; }

	lua_newtable(L);
	meta->toLua(L, data);
	return 1;
}

static int lua_ecs_set(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;

	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	const char* name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);

	uint32_t id = ecs::ComponentRegistry::instance().find(name);
	if (id == ecs::kInvalidEntity) return 0;

	auto* meta = findCompMeta(id);
	if (!meta || !meta->fromLua) return 0;

	void* data = world->entities().get(e, id);
	if (!data) return 0;

	meta->fromLua(L, 3, data);
	return 0;
}

static int lua_ecs_add(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;

	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	const char* name = luaL_checkstring(L, 2);
	uint32_t id = ecs::ComponentRegistry::instance().find(name);
	if (id == ecs::kInvalidEntity) return 0;

	auto* data = world->entities().add(e, id);
	if (data && lua_gettop(L) >= 3 && lua_istable(L, 3)) {
		auto* meta = findCompMeta(id);
		if (meta && meta->fromLua) meta->fromLua(L, 3, data);
	}
	return 0;
}

static int lua_ecs_remove(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	const char* name = luaL_checkstring(L, 2);
	uint32_t id = ecs::ComponentRegistry::instance().find(name);
	if (id != ecs::kInvalidEntity) world->entities().remove(e, id);
	return 0;
}

static int lua_ecs_get_position(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) { lua_pushnumber(L,0);lua_pushnumber(L,0);lua_pushnumber(L,0); return 3; }
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	auto* t = world->get<ecs::TransformComponent>(e);
	if (!t) { lua_pushnumber(L,0);lua_pushnumber(L,0);lua_pushnumber(L,0); return 3; }
	lua_pushnumber(L, t->position.x);
	lua_pushnumber(L, t->position.y);
	lua_pushnumber(L, t->position.z);
	return 3;
}

static int lua_ecs_set_position(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	float x = (float)luaL_checknumber(L, 2);
	float y = (float)luaL_checknumber(L, 3);
	float z = (float)luaL_checknumber(L, 4);
	auto* t = world->get<ecs::TransformComponent>(e);
	if (!t) t = world->add<ecs::TransformComponent>(e);
	if (t) t->position = {x, y, z};
	return 0;
}

static int lua_ecs_set_rotation(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;
	ecs::Entity e = (ecs::Entity)luaL_checkinteger(L, 1);
	if (lua_istable(L, 2)) {
		auto q = toQuat(L, 2);
		auto* t = world->get<ecs::TransformComponent>(e);
		if (!t) t = world->add<ecs::TransformComponent>(e);
		if (t) t->rotation = q;
	} else {
		float pitch = (float)luaL_checknumber(L, 2);
		float yaw   = (float)luaL_checknumber(L, 3);
		float roll  = (float)luaL_optnumber(L, 4, 0);
		auto* t = world->get<ecs::TransformComponent>(e);
		if (!t) t = world->add<ecs::TransformComponent>(e);
		if (t) t->rotation = glm::quat(glm::vec3(pitch, yaw, roll));
	}
	return 0;
}

static int lua_ecs_query(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) { lua_newtable(L); return 1; }

	std::vector<uint32_t> compIds;
	if (lua_istable(L, 1)) {
		lua_pushnil(L);
		while (lua_next(L, 1)) {
			const char* name = lua_tostring(L, -1);
			if (name) {
				uint32_t id = ecs::ComponentRegistry::instance().find(name);
				if (id != ecs::kInvalidEntity) compIds.push_back(id);
			}
			lua_pop(L, 1);
		}
	}

	if (compIds.empty()) { lua_newtable(L); return 1; }

	uint64_t bloom = 0;
	for (auto id : compIds) bloom |= ecs::componentBloomBits(id);

	std::vector<ecs::Entity> results;
	for (const auto& arch : world->entities().archetypes()) {
		if ((arch.bloom & bloom) != bloom) continue;

		std::vector<int> slots;
		bool all = true;
		for (auto id : compIds) {
			int s = arch.slot(id);
			if (s < 0) { all = false; break; }
			slots.push_back(s);
		}
		if (!all) continue;

		for (size_t ci = 0; ci < arch.chunks.size(); ++ci) {
			for (uint32_t i = 0; i < arch.chunks[ci].count; ++i) {
				results.push_back(arch.chunks[ci].entities[i]);
			}
		}
	}

	lua_newtable(L);
	for (size_t i = 0; i < results.size(); ++i) {
		lua_pushinteger(L, results[i]);
		lua_rawseti(L, -2, (int)i + 1);
	}
	return 1;
}

static int lua_ecs_instantiate(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) { lua_pushinteger(L, 0); return 1; }
	const char* name = luaL_checkstring(L, 1);
	auto e = world->instantiate(name);
	lua_pushinteger(L, e);
	return 1;
}

static int lua_ecs_load_templates(lua_State* L) {
	auto* world = getWorld(L);
	if (!world) return 0;
	const char* path = luaL_checkstring(L, 1);
	std::ifstream f(path);
	if (!f) {
		std::cerr << "[Lua] Cannot open template file: " << path << std::endl;
		return 0;
	}
	std::stringstream buf;
	buf << f.rdbuf();
	std::string err;
	if (!world->templates().loadFromString(buf.str(), &err)) {
		std::cerr << "[Lua] Template error: " << err << std::endl;
	}
	return 0;
}

void registerECS(lua_State* L, ecs::World* world) {
	if (world) {
		lua_pushlightuserdata(L, world);
		lua_setfield(L, LUA_REGISTRYINDEX, "__tucano_world");
	}

	registerCompMeta("Transform", ecs::kCompTransform, sizeof(ecs::TransformComponent),
	                 transformToLua, transformFromLua);

	lua_newtable(L);
	lua_pushcfunction(L, lua_ecs_create);       lua_setfield(L, -2, "create");
	lua_pushcfunction(L, lua_ecs_destroy);      lua_setfield(L, -2, "destroy");
	lua_pushcfunction(L, lua_ecs_alive);        lua_setfield(L, -2, "alive");
	lua_pushcfunction(L, lua_ecs_has);          lua_setfield(L, -2, "has");
	lua_pushcfunction(L, lua_ecs_get);          lua_setfield(L, -2, "get");
	lua_pushcfunction(L, lua_ecs_set);          lua_setfield(L, -2, "set");
	lua_pushcfunction(L, lua_ecs_add);          lua_setfield(L, -2, "add");
	lua_pushcfunction(L, lua_ecs_remove);       lua_setfield(L, -2, "remove");
	lua_pushcfunction(L, lua_ecs_get_position); lua_setfield(L, -2, "get_position");
	lua_pushcfunction(L, lua_ecs_set_position); lua_setfield(L, -2, "set_position");
	lua_pushcfunction(L, lua_ecs_set_rotation); lua_setfield(L, -2, "set_rotation");
	lua_pushcfunction(L, lua_ecs_query);        lua_setfield(L, -2, "query");
	lua_pushcfunction(L, lua_ecs_instantiate);  lua_setfield(L, -2, "instantiate");
	lua_pushcfunction(L, lua_ecs_load_templates); lua_setfield(L, -2, "load_templates");
	lua_setglobal(L, "ecs");
}

// ── registerInput ─────────────────────────────────────

static int lua_input_key_down(lua_State* L) {
	auto* input = getInput(L);
	const char* key = luaL_checkstring(L, 1);
	auto code = input::buttonCodeFromName(key);
	lua_pushboolean(L, input ? input->keyPressed(static_cast<int>(code)) : false);
	return 1;
}

static int lua_input_key_held(lua_State* L) {
	auto* input = getInput(L);
	const char* key = luaL_checkstring(L, 1);
	auto code = input::buttonCodeFromName(key);
	lua_pushboolean(L, input ? input->keyDown(static_cast<int>(code)) : false);
	return 1;
}

static int lua_input_mouse_down(lua_State* L) {
	auto* input = getInput(L);
	int btn = (int)luaL_checkinteger(L, 1);
	int code = 0;
	switch (btn) {
		case 0: code = static_cast<int>(input::ButtonCode::MouseLeft); break;
		case 1: code = static_cast<int>(input::ButtonCode::MouseRight); break;
		case 2: code = static_cast<int>(input::ButtonCode::MouseMiddle); break;
		default: lua_pushboolean(L, false); return 1;
	}
	lua_pushboolean(L, input ? input->mousePressed(code) : false);
	return 1;
}

static int lua_input_mouse_held(lua_State* L) {
	auto* input = getInput(L);
	int btn = (int)luaL_checkinteger(L, 1);
	int code = 0;
	switch (btn) {
		case 0: code = static_cast<int>(input::ButtonCode::MouseLeft); break;
		case 1: code = static_cast<int>(input::ButtonCode::MouseRight); break;
		case 2: code = static_cast<int>(input::ButtonCode::MouseMiddle); break;
		default: lua_pushboolean(L, false); return 1;
	}
	lua_pushboolean(L, input ? input->mouseDown(code) : false);
	return 1;
}

static int lua_input_mouse_pos(lua_State* L) {
	auto* input = getInput(L);
	float x = 0, y = 0;
	if (input) input->mousePosition(x, y);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	return 2;
}

static int lua_input_mouse_delta(lua_State* L) {
	auto* input = getInput(L);
	float dx = 0, dy = 0;
	if (input) input->mouseDelta(dx, dy);
	lua_pushnumber(L, dx);
	lua_pushnumber(L, dy);
	return 2;
}

static int lua_input_scroll(lua_State* L) {
	auto* input = getInput(L);
	lua_pushnumber(L, input ? input->scrollY() : 0);
	return 1;
}

void registerInput(lua_State* L, Input* input) {
	if (input) {
		lua_pushlightuserdata(L, input);
		lua_setfield(L, LUA_REGISTRYINDEX, "__tucano_input");
	}

	lua_newtable(L);
	lua_pushcfunction(L, lua_input_key_down);    lua_setfield(L, -2, "key_down");
	lua_pushcfunction(L, lua_input_key_held);    lua_setfield(L, -2, "key_held");
	lua_pushcfunction(L, lua_input_mouse_down);  lua_setfield(L, -2, "mouse_down");
	lua_pushcfunction(L, lua_input_mouse_held);  lua_setfield(L, -2, "mouse_held");
	lua_pushcfunction(L, lua_input_mouse_pos);   lua_setfield(L, -2, "mouse_position");
	lua_pushcfunction(L, lua_input_mouse_delta); lua_setfield(L, -2, "mouse_delta");
	lua_pushcfunction(L, lua_input_scroll);      lua_setfield(L, -2, "scroll");
	lua_setglobal(L, "input");
}

// ── registerPhysics ───────────────────────────────────

static int lua_physics_raycast(lua_State* L) {
	auto* phys = getPhysics(L);
	if (!phys) { lua_pushboolean(L, false); return 1; }

	auto origin = toVec3(L, 1);
	auto dir = toVec3(L, 2);
	float maxDist = (float)luaL_checknumber(L, 3);

	float hitDist = 0;
	glm::vec3 hitNormal;
	bool hit = phys->rayCast(origin, dir, maxDist, hitDist, hitNormal);

	if (hit) {
		lua_pushboolean(L, true);
		lua_pushnumber(L, hitDist);
		pushVec3(L, hitNormal);
		glm::vec3 point = origin + dir * hitDist;
		pushVec3(L, point);
		return 4;
	}
	lua_pushboolean(L, false);
	return 1;
}

static int lua_physics_screen_ray(lua_State* L) {
	auto* camera = getCamera(L);
	if (!camera) { lua_pushboolean(L, false); return 1; }

	float sx = (float)luaL_checknumber(L, 1);
	float sy = (float)luaL_checknumber(L, 2);
	float vpW = (float)luaL_checknumber(L, 3);
	float vpH = (float)luaL_checknumber(L, 4);

	glm::vec3 origin, dir;
	camera->screenToWorldRay(sx, sy, vpW, vpH, origin, dir);

	lua_pushnumber(L, origin.x);
	lua_pushnumber(L, origin.y);
	lua_pushnumber(L, origin.z);
	lua_pushnumber(L, dir.x);
	lua_pushnumber(L, dir.y);
	lua_pushnumber(L, dir.z);
	return 6;
}

void registerPhysics(lua_State* L, physics::PhysicsWorld* phys, Camera* camera) {
	if (phys) {
		lua_pushlightuserdata(L, phys);
		lua_setfield(L, LUA_REGISTRYINDEX, "__tucano_physics");
	}
	if (camera) {
		lua_pushlightuserdata(L, camera);
		lua_setfield(L, LUA_REGISTRYINDEX, "__tucano_camera");
	}

	lua_newtable(L);
	lua_pushcfunction(L, lua_physics_raycast);    lua_setfield(L, -2, "raycast");
	lua_pushcfunction(L, lua_physics_screen_ray); lua_setfield(L, -2, "screen_ray");
	lua_setglobal(L, "physics");
}

// ── registerState ─────────────────────────────────────

void registerState(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		bool def = lua_toboolean(Ls, 2);
		lua_pushboolean(Ls, st->getBool(key, def));
		return 1;
	}); lua_setfield(L, -2, "get_bool");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		int def = (int)luaL_optinteger(Ls, 2, 0);
		lua_pushinteger(Ls, st->getInt(key, def));
		return 1;
	}); lua_setfield(L, -2, "get_int");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		float def = (float)luaL_optnumber(Ls, 2, 0);
		lua_pushnumber(Ls, st->getFloat(key, def));
		return 1;
	}); lua_setfield(L, -2, "get_float");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		const char* def = luaL_optstring(Ls, 2, "");
		lua_pushstring(Ls, st->getString(key, def).c_str());
		return 1;
	}); lua_setfield(L, -2, "get_string");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		bool val = lua_toboolean(Ls, 2);
		st->set(key, val);
		return 0;
	}); lua_setfield(L, -2, "set_bool");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		int val = (int)luaL_checkinteger(Ls, 2);
		st->set(key, val);
		return 0;
	}); lua_setfield(L, -2, "set_int");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		float val = (float)luaL_checknumber(Ls, 2);
		st->set(key, val);
		return 0;
	}); lua_setfield(L, -2, "set_float");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		const char* val = luaL_checkstring(Ls, 2);
		st->set(key, std::string(val));
		return 0;
	}); lua_setfield(L, -2, "set_string");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* key = luaL_checkstring(Ls, 1);
		lua_pushboolean(Ls, st->has(key));
		return 1;
	}); lua_setfield(L, -2, "has");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* path = luaL_checkstring(Ls, 1);
		lua_pushboolean(Ls, st->save(path));
		return 1;
	}); lua_setfield(L, -2, "save");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* st = &StateStorage::instance();
		const char* path = luaL_checkstring(Ls, 1);
		lua_pushboolean(Ls, st->load(path));
		return 1;
	}); lua_setfield(L, -2, "load");

	lua_pushcfunction(L, [](lua_State*) -> int {
		StateStorage::instance().clear();
		return 0;
	}); lua_setfield(L, -2, "clear");

	lua_setglobal(L, "state");
}

// ── registerTween ─────────────────────────────────────

void registerTween(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		float x = (float)luaL_checknumber(Ls, 2);
		float y = (float)luaL_checknumber(Ls, 3);
		float z = (float)luaL_checknumber(Ls, 4);
		float duration = (float)luaL_checknumber(Ls, 5);
		const char* easing = luaL_optstring(Ls, 6, "linear");
		TweenSystem::instance().tweenPosition(e, {x,y,z}, duration, easing);
		return 0;
	}); lua_setfield(L, -2, "position");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		float w = (float)luaL_checknumber(Ls, 2);
		float x = (float)luaL_checknumber(Ls, 3);
		float y = (float)luaL_checknumber(Ls, 4);
		float z = (float)luaL_checknumber(Ls, 5);
		float duration = (float)luaL_checknumber(Ls, 6);
		const char* easing = luaL_optstring(Ls, 7, "linear");
		TweenSystem::instance().tweenRotation(e, {w,x,y,z}, duration, easing);
		return 0;
	}); lua_setfield(L, -2, "rotation");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		float x = (float)luaL_checknumber(Ls, 2);
		float y = (float)luaL_checknumber(Ls, 3);
		float z = (float)luaL_checknumber(Ls, 4);
		float duration = (float)luaL_checknumber(Ls, 5);
		const char* easing = luaL_optstring(Ls, 6, "linear");
		TweenSystem::instance().tweenScale(e, {x,y,z}, duration, easing);
		return 0;
	}); lua_setfield(L, -2, "scale");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		TweenSystem::instance().kill(e);
		return 0;
	}); lua_setfield(L, -2, "kill");

	lua_pushcfunction(L, [](lua_State*) -> int {
		TweenSystem::instance().killAll();
		return 0;
	}); lua_setfield(L, -2, "kill_all");

	lua_setglobal(L, "tween");
}

// ── registerAudio ─────────────────────────────────────

static std::vector<std::unique_ptr<AudioSource>> g_audioSources;
static std::vector<std::unique_ptr<AudioClip>> g_audioClips;

void registerAudio(lua_State* L, Audio* audio) {
	if (audio) {
		lua_pushlightuserdata(L, audio);
		lua_setfield(L, LUA_REGISTRYINDEX, "__tucano_audio");
	}

	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* path = luaL_checkstring(Ls, 1);
		float volume = (float)luaL_optnumber(Ls, 2, 1.0);
		bool loop = lua_toboolean(Ls, 3);

		AudioClip* clip = AudioClip::loadWav(path);
		if (!clip) {
			lua_pushinteger(Ls, -1);
			return 1;
		}
		auto clipPtr = std::unique_ptr<AudioClip>(clip);
		auto source = std::make_unique<AudioSource>();
		source->play(clipPtr.get(), volume, loop);

		int32_t id = (int32_t)g_audioSources.size();
		g_audioClips.push_back(std::move(clipPtr));
		g_audioSources.push_back(std::move(source));
		lua_pushinteger(Ls, id);
		return 1;
	}); lua_setfield(L, -2, "play_2d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* path = luaL_checkstring(Ls, 1);
		auto pos = toVec3(Ls, 2);
		float volume = (float)luaL_optnumber(Ls, 3, 1.0);
		bool loop = lua_toboolean(Ls, 4);

		AudioClip* clip = AudioClip::loadWav(path);
		if (!clip) {
			lua_pushinteger(Ls, -1);
			return 1;
		}
		auto clipPtr = std::unique_ptr<AudioClip>(clip);
		auto source = std::make_unique<AudioSource>();
		source->setPosition(pos);
		source->play(clipPtr.get(), volume, loop);

		int32_t id = (int32_t)g_audioSources.size();
		g_audioClips.push_back(std::move(clipPtr));
		g_audioSources.push_back(std::move(source));
		lua_pushinteger(Ls, id);
		return 1;
	}); lua_setfield(L, -2, "play_3d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		int32_t id = (int32_t)luaL_checkinteger(Ls, 1);
		if (id >= 0 && id < (int32_t)g_audioSources.size())
			g_audioSources[id]->stop();
		return 0;
	}); lua_setfield(L, -2, "stop");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		int32_t id = (int32_t)luaL_checkinteger(Ls, 1);
		float vol = (float)luaL_checknumber(Ls, 2);
		if (id >= 0 && id < (int32_t)g_audioSources.size())
			g_audioSources[id]->setVolume(vol);
		return 0;
	}); lua_setfield(L, -2, "set_volume");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		int32_t id = (int32_t)luaL_checkinteger(Ls, 1);
		auto pos = toVec3(Ls, 2);
		if (id >= 0 && id < (int32_t)g_audioSources.size())
			g_audioSources[id]->setPosition(pos);
		return 0;
	}); lua_setfield(L, -2, "set_position");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* a = getAudio(Ls);
		float vol = (float)luaL_checknumber(Ls, 1);
		if (a) a->setMasterVolume(vol);
		return 0;
	}); lua_setfield(L, -2, "set_master_volume");

	// Sound events (fire-and-forget)
	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* cueName = luaL_checkstring(Ls, 1);
		SoundEvents::instance().play(cueName);
		return 0;
	}); lua_setfield(L, -2, "play_cue");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* cueName = luaL_checkstring(Ls, 1);
		auto pos = toVec3(Ls, 2);
		SoundEvents::instance().play3D(cueName, pos);
		return 0;
	}); lua_setfield(L, -2, "play_cue_3d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* cueName = luaL_checkstring(Ls, 1);
		const char* path = luaL_checkstring(Ls, 2);
		SoundEvents::instance().registerCue(cueName, path);
		return 0;
	}); lua_setfield(L, -2, "register_cue");

	lua_setglobal(L, "audio");
}

// ── registerAnimation ─────────────────────────────────

void registerAnimation(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		(void)luaL_checkinteger(Ls, 1);
		(void)luaL_checkstring(Ls, 2);
		(void)lua_toboolean(Ls, 3);
		lua_pushboolean(Ls, false);
		return 1;
	}); lua_setfield(L, -2, "play");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		(void)luaL_checkinteger(Ls, 1);
		return 0;
	}); lua_setfield(L, -2, "stop");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		(void)luaL_checkinteger(Ls, 1);
		(void)luaL_checknumber(Ls, 2);
		return 0;
	}); lua_setfield(L, -2, "set_speed");

	lua_setglobal(L, "anim");
}

// ── registerRender2D ──────────────────────────────────

void registerRender2D(lua_State* L, Renderer* renderer) {
	(void)renderer;

	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		luaL_checkstring(Ls, 1);
		luaL_checknumber(Ls, 2);
		luaL_checknumber(Ls, 3);
		luaL_checknumber(Ls, 4);
		luaL_optnumber(Ls, 5, 0);
		return 0;
	}); lua_setfield(L, -2, "draw_sprite");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		luaL_checknumber(Ls, 1);
		luaL_checknumber(Ls, 2);
		luaL_checknumber(Ls, 3);
		luaL_checknumber(Ls, 4);
		luaL_optnumber(Ls, 5, 1);
		luaL_optnumber(Ls, 6, 1);
		luaL_optnumber(Ls, 7, 1);
		luaL_optnumber(Ls, 8, 1);
		return 0;
	}); lua_setfield(L, -2, "draw_rect");

	lua_newtable(L);
	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		luaL_checkstring(Ls, 1);
		luaL_checknumber(Ls, 2);
		luaL_checknumber(Ls, 3);
		luaL_optnumber(Ls, 4, 16);
		(void)lua_istable(Ls, 5);
		return 0;
	}); lua_setfield(L, -2, "text");
	lua_setfield(L, -2, "font");

	lua_setglobal(L, "render");
}

// ── registerEvents ────────────────────────────────────

static std::unordered_map<std::string, std::vector<int>> g_eventRefs;
static lua_State* g_eventLuaState = nullptr;

void registerEvents(lua_State* L) {
	g_eventLuaState = L;
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* event = luaL_checkstring(Ls, 1);
		luaL_checktype(Ls, 2, LUA_TFUNCTION);

		lua_pushvalue(Ls, 2);
		int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
		g_eventRefs[event].push_back(ref);

		EventBus::instance().on(event, [event](const std::string&, std::any) {
			if (!g_eventLuaState) return;
			for (int ref : g_eventRefs[event]) {
				lua_rawgeti(g_eventLuaState, LUA_REGISTRYINDEX, ref);
				if (lua_pcall(g_eventLuaState, 0, 0, 0) != LUA_OK) {
					std::cerr << "[Lua] Event error: " << lua_tostring(g_eventLuaState, -1) << std::endl;
					lua_pop(g_eventLuaState, 1);
				}
			}
		});

		lua_pushboolean(Ls, true);
		return 1;
	}); lua_setfield(L, -2, "on");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* event = luaL_checkstring(Ls, 1);
		EventBus::instance().emit(event);
		return 0;
	}); lua_setfield(L, -2, "emit");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* event = luaL_checkstring(Ls, 1);
		EventBus::instance().off(event);
		return 0;
	}); lua_setfield(L, -2, "off");

	lua_pushcfunction(L, [](lua_State*) -> int {
		EventBus::instance().clear();
		return 0;
	}); lua_setfield(L, -2, "clear");

	lua_setglobal(L, "events");
}

// ── registerTrigger ───────────────────────────────────

void registerTrigger(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto pos = toVec3(Ls, 1);
		auto he = toVec3(Ls, 2);
		auto id = physics::TriggerSystem::instance().createBox(pos, he);
		lua_pushinteger(Ls, id);
		return 1;
	}); lua_setfield(L, -2, "create_box");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto pos = toVec3(Ls, 1);
		float radius = (float)luaL_checknumber(Ls, 2);
		auto id = physics::TriggerSystem::instance().createSphere(pos, radius);
		lua_pushinteger(Ls, id);
		return 1;
	}); lua_setfield(L, -2, "create_sphere");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t id = (uint32_t)luaL_checkinteger(Ls, 1);
		physics::TriggerSystem::instance().remove(id);
		return 0;
	}); lua_setfield(L, -2, "remove");

	lua_setglobal(L, "trigger");
}

// ── registerPool ──────────────────────────────────────

void registerPool(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* tmpl = luaL_checkstring(Ls, 1);
		uint32_t count = (uint32_t)luaL_checkinteger(Ls, 2);
		ObjectPool::instance().prewarm(tmpl, count);
		return 0;
	}); lua_setfield(L, -2, "prewarm");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* tmpl = luaL_checkstring(Ls, 1);
		auto e = ObjectPool::instance().acquire(tmpl);
		lua_pushinteger(Ls, e);
		return 1;
	}); lua_setfield(L, -2, "acquire");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* tmpl = luaL_checkstring(Ls, 1);
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 2);
		ObjectPool::instance().release(tmpl, e);
		return 0;
	}); lua_setfield(L, -2, "release");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* tmpl = luaL_checkstring(Ls, 1);
		lua_pushinteger(Ls, ObjectPool::instance().available(tmpl));
		return 1;
	}); lua_setfield(L, -2, "available");

	lua_pushcfunction(L, [](lua_State*) -> int {
		ObjectPool::instance().clear();
		return 0;
	}); lua_setfield(L, -2, "clear");

	lua_setglobal(L, "pool");
}

// ── registerCamera ────────────────────────────────────

void registerCamera(lua_State* L, Camera*) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* cam = getCamera(Ls);
		lua_pushnumber(Ls, cam ? cam->position().x : 0);
		lua_pushnumber(Ls, cam ? cam->position().y : 0);
		lua_pushnumber(Ls, cam ? cam->position().z : 0);
		return 3;
	}); lua_setfield(L, -2, "get_position");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* cam = getCamera(Ls);
		if (!cam) return 0;
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		cam->setPosition({x, y, z});
		return 0;
	}); lua_setfield(L, -2, "set_position");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* cam = getCamera(Ls);
		if (!cam) { lua_pushnumber(Ls,0);lua_pushnumber(Ls,0);lua_pushnumber(Ls,0); return 3; }
		auto f = cam->forward();
		lua_pushnumber(Ls, f.x); lua_pushnumber(Ls, f.y); lua_pushnumber(Ls, f.z);
		return 3;
	}); lua_setfield(L, -2, "get_forward");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* cam = getCamera(Ls);
		if (!cam) return 0;
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		cam->lookAt({x, y, z});
		return 0;
	}); lua_setfield(L, -2, "look_at");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		float intensity = (float)luaL_checknumber(Ls, 1);
		float duration = (float)luaL_checknumber(Ls, 2);
		float freq = (float)luaL_optnumber(Ls, 3, 10.0);
		float roughness = (float)luaL_optnumber(Ls, 4, 0.5);
		CameraShake::instance().add(intensity, duration, freq, roughness);
		return 0;
	}); lua_setfield(L, -2, "shake");

	lua_pushcfunction(L, [](lua_State*) -> int {
		CameraShake::instance().stop();
		return 0;
	}); lua_setfield(L, -2, "shake_stop");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto offset = CameraShake::instance().offset();
		lua_pushnumber(Ls, offset.x);
		lua_pushnumber(Ls, offset.y);
		lua_pushnumber(Ls, offset.z);
		return 3;
	}); lua_setfield(L, -2, "shake_offset");

	lua_setglobal(L, "camera");
}

// ── registerSpline ────────────────────────────────────

void registerSpline(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = new SplinePath();

		if (lua_istable(Ls, 1)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 1)) {
				if (lua_istable(Ls, -1)) {
					auto p = toVec3(Ls, -1);
					spline->addPoint(p);
				}
				lua_pop(Ls, 1);
			}
		}

		lua_pushlightuserdata(Ls, spline);
		return 1;
	}); lua_setfield(L, -2, "new");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = static_cast<SplinePath*>(lua_touserdata(Ls, 1));
		float t = (float)luaL_checknumber(Ls, 2);
		if (!spline) { pushVec3(Ls, {0,0,0}); return 1; }
		pushVec3(Ls, spline->evaluate(t));
		return 1;
	}); lua_setfield(L, -2, "evaluate");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = static_cast<SplinePath*>(lua_touserdata(Ls, 1));
		float t = (float)luaL_checknumber(Ls, 2);
		if (!spline) { pushVec3(Ls, {0,0,1}); return 1; }
		pushVec3(Ls, spline->tangent(t));
		return 1;
	}); lua_setfield(L, -2, "tangent");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = static_cast<SplinePath*>(lua_touserdata(Ls, 1));
		if (!spline) { lua_pushnumber(Ls, 0); return 1; }
		lua_pushnumber(Ls, spline->length());
		return 1;
	}); lua_setfield(L, -2, "length");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = static_cast<SplinePath*>(lua_touserdata(Ls, 1));
		auto pt = toVec3(Ls, 2);
		if (!spline) { lua_pushnumber(Ls, 0); return 1; }
		lua_pushnumber(Ls, spline->closestParam(pt));
		return 1;
	}); lua_setfield(L, -2, "closest_param");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* spline = static_cast<SplinePath*>(lua_touserdata(Ls, 1));
		delete spline;
		return 0;
	}); lua_setfield(L, -2, "destroy");

	lua_setglobal(L, "spline");
}

// ── registerNetwork ───────────────────────────────────

void registerNetwork(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint16_t port = (uint16_t)luaL_optinteger(Ls, 1, 27015);
		uint32_t maxClients = (uint32_t)luaL_optinteger(Ls, 2, 32);
		net::NetworkConfig cfg;
		cfg.port = port;
		cfg.maxClients = maxClients;
		net::NetworkManager::instance().configure(cfg);
		lua_pushboolean(Ls, net::NetworkManager::instance().startServer());
		return 1;
	}); lua_setfield(L, -2, "start_server");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* host = luaL_checkstring(Ls, 1);
		uint16_t port = (uint16_t)luaL_optinteger(Ls, 2, 27015);
		net::NetworkConfig cfg;
		cfg.port = port;
		net::NetworkManager::instance().configure(cfg);
		lua_pushboolean(Ls, net::NetworkManager::instance().connectClient(host));
		return 1;
	}); lua_setfield(L, -2, "connect");

	lua_pushcfunction(L, [](lua_State*) -> int {
		net::NetworkManager::instance().stop();
		return 0;
	}); lua_setfield(L, -2, "stop");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		lua_pushboolean(Ls, net::NetworkManager::instance().isRunning());
		return 1;
	}); lua_setfield(L, -2, "is_running");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		lua_pushboolean(Ls, net::NetworkManager::instance().isServer());
		return 1;
	}); lua_setfield(L, -2, "is_server");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		lua_pushboolean(Ls, net::NetworkManager::instance().isClient());
		return 1;
	}); lua_setfield(L, -2, "is_client");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		lua_pushinteger(Ls, net::NetworkManager::instance().clientCount());
		return 1;
	}); lua_setfield(L, -2, "client_count");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t clientId = (uint32_t)luaL_checkinteger(Ls, 1);
		lua_pushnumber(Ls, net::NetworkManager::instance().latency(clientId));
		return 1;
	}); lua_setfield(L, -2, "latency");

	lua_setglobal(L, "net");
}

// ── registerRPC ───────────────────────────────────────

void registerRPC(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t rpcId = (uint32_t)luaL_checkinteger(Ls, 1);
		const char* data = luaL_optstring(Ls, 2, "");
		net::RPCSystem::instance().callServer(rpcId, (const uint8_t*)data, (uint32_t)strlen(data));
		return 0;
	}); lua_setfield(L, -2, "call_server");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t target = (uint32_t)luaL_checkinteger(Ls, 1);
		uint32_t rpcId = (uint32_t)luaL_checkinteger(Ls, 2);
		const char* data = luaL_optstring(Ls, 3, "");
		net::RPCSystem::instance().callClient(target, rpcId, (const uint8_t*)data, (uint32_t)strlen(data));
		return 0;
	}); lua_setfield(L, -2, "call_client");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t rpcId = (uint32_t)luaL_checkinteger(Ls, 1);
		const char* data = luaL_optstring(Ls, 2, "");
		net::RPCSystem::instance().broadcastRPC(rpcId, (const uint8_t*)data, (uint32_t)strlen(data));
		return 0;
	}); lua_setfield(L, -2, "broadcast");

	lua_setglobal(L, "rpc");
}

// ── registerReplication ───────────────────────────────

void registerReplication(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* compName = luaL_checkstring(Ls, 1);
		net::ReplicationSystem::instance().markReplicated(compName);
		return 0;
	}); lua_setfield(L, -2, "mark_component");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		net::ReplicationSystem::instance().spawnOnClients(e);
		return 0;
	}); lua_setfield(L, -2, "spawn");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		ecs::Entity e = (ecs::Entity)luaL_checkinteger(Ls, 1);
		net::ReplicationSystem::instance().destroyOnClients(e);
		return 0;
	}); lua_setfield(L, -2, "despawn");

	lua_setglobal(L, "replication");
}

// ── registerAnimController ────────────────────────────

void registerAnimController(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = new anim::AnimationController();

		if (lua_istable(Ls, 1)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 1)) {
				anim::AnimState state;
				lua_getfield(Ls, -1, "name");  state.name = lua_tostring(Ls, -1); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "clip");  state.clipName = lua_tostring(Ls, -1); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "speed"); state.speed = (float)luaL_optnumber(Ls, -1, 1.0); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "loop");  state.loop = lua_toboolean(Ls, -1); lua_pop(Ls, 1);
				ctrl->addState(state);
				lua_pop(Ls, 1);
			}
		}

		if (lua_istable(Ls, 2)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 2)) {
				anim::AnimTransition trans;
				lua_getfield(Ls, -1, "from");    std::string from = lua_tostring(Ls, -1); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "to");      trans.targetState = lua_tostring(Ls, -1); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "blend");    trans.blendDuration = (float)luaL_optnumber(Ls, -1, 0.2); lua_pop(Ls, 1);
				ctrl->addTransition(from, trans);
				lua_pop(Ls, 1);
			}
		}

		lua_pushlightuserdata(Ls, ctrl);
		return 1;
	}); lua_setfield(L, -2, "new");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		if (!ctrl) return 0;
		ctrl->update(g_deltaTime);
		return 0;
	}); lua_setfield(L, -2, "update");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		if (!ctrl) { lua_pushstring(Ls, ""); return 1; }
		lua_pushstring(Ls, ctrl->currentClip().c_str());
		return 1;
	}); lua_setfield(L, -2, "current_clip");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		const char* param = luaL_checkstring(Ls, 2);
		float val = (float)luaL_checknumber(Ls, 3);
		if (ctrl) ctrl->setFloat(param, val);
		return 0;
	}); lua_setfield(L, -2, "set_float");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		const char* param = luaL_checkstring(Ls, 2);
		bool val = lua_toboolean(Ls, 3);
		if (ctrl) ctrl->setBool(param, val);
		return 0;
	}); lua_setfield(L, -2, "set_bool");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		const char* param = luaL_checkstring(Ls, 2);
		if (ctrl) ctrl->trigger(param);
		return 0;
	}); lua_setfield(L, -2, "trigger");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		const char* state = luaL_checkstring(Ls, 2);
		float blend = (float)luaL_optnumber(Ls, 3, 0.1);
		if (ctrl) ctrl->forceState(state, blend);
		return 0;
	}); lua_setfield(L, -2, "force_state");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* ctrl = static_cast<anim::AnimationController*>(lua_touserdata(Ls, 1));
		delete ctrl;
		return 0;
	}); lua_setfield(L, -2, "destroy");

	lua_setglobal(L, "anim_ctrl");
}

// ── registerNavMesh ───────────────────────────────────

void registerNavMesh(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* nav = new ai::NavMesh();

		if (lua_istable(Ls, 1)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 1)) {
				if (lua_istable(Ls, -1)) {
					auto p = toVec3(Ls, -1);
					nav->addNode(p);
				}
				lua_pop(Ls, 1);
			}
		}

		if (lua_istable(Ls, 2)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 2)) {
				if (lua_istable(Ls, -1)) {
					lua_rawgeti(Ls, -1, 1);
					uint32_t a = (uint32_t)lua_tointeger(Ls, -1);
					lua_pop(Ls, 1);
					lua_rawgeti(Ls, -1, 2);
					uint32_t b = (uint32_t)lua_tointeger(Ls, -1);
					lua_pop(Ls, 1);
					nav->connectNodes(a, b);
				}
				lua_pop(Ls, 1);
			}
		}

		lua_pushlightuserdata(Ls, nav);
		return 1;
	}); lua_setfield(L, -2, "new");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* nav = static_cast<ai::NavMesh*>(lua_touserdata(Ls, 1));
		auto pt = toVec3(Ls, 2);
		if (!nav) { lua_pushinteger(Ls, 0); return 1; }
		lua_pushinteger(Ls, nav->closestNode(pt));
		return 1;
	}); lua_setfield(L, -2, "closest_node");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* nav = static_cast<ai::NavMesh*>(lua_touserdata(Ls, 1));
		auto from = toVec3(Ls, 2);
		auto to = toVec3(Ls, 3);
		if (!nav) { lua_newtable(Ls); return 1; }

		auto path = nav->findPathFromTo(from, to);
		lua_newtable(Ls);
		for (size_t i = 0; i < path.size(); ++i) {
			lua_newtable(Ls);
			pushVec3(Ls, path[i].position);
			lua_rawseti(Ls, -2, (int)i + 1);
		}
		return 1;
	}); lua_setfield(L, -2, "find_path");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* nav = static_cast<ai::NavMesh*>(lua_touserdata(Ls, 1));
		delete nav;
		return 0;
	}); lua_setfield(L, -2, "destroy");

	lua_setglobal(L, "navmesh");
}

// ── registerCoroutine ─────────────────────────────────

void registerCoroutine(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		float seconds = (float)luaL_checknumber(Ls, 1);
		luaL_checktype(Ls, 2, LUA_TFUNCTION);
		lua_pushvalue(Ls, 2);
		int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);

		CoroutineSystem::instance().wait(seconds, [ref, Ls]() {
			lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
			if (lua_pcall(Ls, 0, 0, 0) != LUA_OK) {
				std::cerr << "[Lua] Coroutine error: " << lua_tostring(Ls, -1) << std::endl;
				lua_pop(Ls, 1);
			}
			luaL_unref(Ls, LUA_REGISTRYINDEX, ref);
		});

		return 0;
	}); lua_setfield(L, -2, "wait");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		uint32_t frames = (uint32_t)luaL_checkinteger(Ls, 1);
		luaL_checktype(Ls, 2, LUA_TFUNCTION);
		lua_pushvalue(Ls, 2);
		int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);

		CoroutineSystem::instance().waitFrames(frames, [ref, Ls]() {
			lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
			if (lua_pcall(Ls, 0, 0, 0) != LUA_OK) {
				std::cerr << "[Lua] Coroutine error: " << lua_tostring(Ls, -1) << std::endl;
				lua_pop(Ls, 1);
			}
			luaL_unref(Ls, LUA_REGISTRYINDEX, ref);
		});

		return 0;
	}); lua_setfield(L, -2, "wait_frames");

	lua_pushcfunction(L, [](lua_State*) -> int {
		return 0;
	}); lua_setfield(L, -2, "start");

	lua_setglobal(L, "coroutine");
}

// ── registerBlendSpace ────────────────────────────────

void registerBlendSpace(lua_State* L) {
	lua_newtable(L);

	// 1D BlendSpace
	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* paramName = luaL_checkstring(Ls, 1);
		float pmin = (float)luaL_optnumber(Ls, 2, 0);
		float pmax = (float)luaL_optnumber(Ls, 3, 1);

		auto* bs = new anim::BlendSpace1D();
		bs->setParamName(paramName);
		bs->setParamRange(pmin, pmax);
		lua_pushlightuserdata(Ls, bs);
		return 1;
	}); lua_setfield(L, -2, "new_1d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* bs = static_cast<anim::BlendSpace1D*>(lua_touserdata(Ls, 1));
		float val = (float)luaL_checknumber(Ls, 2);
		auto* clip = static_cast<anim::AnimationClip*>(lua_touserdata(Ls, 3));
		float rate = (float)luaL_optnumber(Ls, 4, 1.0);
		if (bs && clip) bs->addSample(val, clip, rate);
		return 0;
	}); lua_setfield(L, -2, "add_sample_1d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* bs = static_cast<anim::BlendSpace1D*>(lua_touserdata(Ls, 1));
		delete bs;
		return 0;
	}); lua_setfield(L, -2, "destroy_1d");

	// 2D BlendSpace
	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* bs = new anim::BlendSpace2D();
		lua_pushlightuserdata(Ls, bs);
		return 1;
	}); lua_setfield(L, -2, "new_2d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* bs = static_cast<anim::BlendSpace2D*>(lua_touserdata(Ls, 1));
		float x = (float)luaL_checknumber(Ls, 2);
		float y = (float)luaL_checknumber(Ls, 3);
		auto* clip = static_cast<anim::AnimationClip*>(lua_touserdata(Ls, 4));
		float rate = (float)luaL_optnumber(Ls, 5, 1.0);
		if (bs && clip) bs->addSample(x, y, clip, rate);
		return 0;
	}); lua_setfield(L, -2, "add_sample_2d");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* bs = static_cast<anim::BlendSpace2D*>(lua_touserdata(Ls, 1));
		delete bs;
		return 0;
	}); lua_setfield(L, -2, "destroy_2d");

	lua_setglobal(L, "blendspace");
}

// ── registerAnimLayers ────────────────────────────────

void registerAnimLayers(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layers = new anim::AnimationLayers();
		lua_pushlightuserdata(Ls, layers);
		return 1;
	}); lua_setfield(L, -2, "new");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layers = static_cast<anim::AnimationLayers*>(lua_touserdata(Ls, 1));
		const char* name = luaL_checkstring(Ls, 2);
		if (layers) {
			auto& layer = layers->addLayer(name);
			lua_pushlightuserdata(Ls, &layer);
		} else {
			lua_pushnil(Ls);
		}
		return 1;
	}); lua_setfield(L, -2, "add_layer");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		auto* clip = static_cast<anim::AnimationClip*>(lua_touserdata(Ls, 2));
		bool loop = lua_toboolean(Ls, 3);
		float speed = (float)luaL_optnumber(Ls, 4, 1.0);
		if (layer) layer->play(clip, loop, speed);
		return 0;
	}); lua_setfield(L, -2, "play");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		float fadeOut = (float)luaL_optnumber(Ls, 2, 0.2);
		if (layer) layer->stop(fadeOut);
		return 0;
	}); lua_setfield(L, -2, "stop");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		float w = (float)luaL_checknumber(Ls, 2);
		if (layer) { layer->targetWeight = w; }
		return 0;
	}); lua_setfield(L, -2, "set_weight");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		if (layer) layer->mask = anim::BoneMask::upperBody();
		return 0;
	}); lua_setfield(L, -2, "set_mask_upper");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		if (layer) layer->mask = anim::BoneMask::lowerBody();
		return 0;
	}); lua_setfield(L, -2, "set_mask_lower");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		const char* boneName = luaL_checkstring(Ls, 2);
		if (layer) layer->mask.includeBone(boneName);
		return 0;
	}); lua_setfield(L, -2, "include_bone");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		if (layer) layer->mask = anim::BoneMask::fullBody();
		return 0;
	}); lua_setfield(L, -2, "set_mask_full");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layer = static_cast<anim::AnimationLayer*>(lua_touserdata(Ls, 1));
		if (layer) layer->additive = true;
		return 0;
	}); lua_setfield(L, -2, "set_additive");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layers = static_cast<anim::AnimationLayers*>(lua_touserdata(Ls, 1));
		float dt = (float)luaL_checknumber(Ls, 2);
		if (layers) layers->update(dt);
		return 0;
	}); lua_setfield(L, -2, "update");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layers = static_cast<anim::AnimationLayers*>(lua_touserdata(Ls, 1));
		const char* name = luaL_checkstring(Ls, 2);
		if (layers) layers->removeLayer(name);
		return 0;
	}); lua_setfield(L, -2, "remove_layer");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		auto* layers = static_cast<anim::AnimationLayers*>(lua_touserdata(Ls, 1));
		delete layers;
		return 0;
	}); lua_setfield(L, -2, "destroy");

	lua_setglobal(L, "anim_layers");
}

// ── registerAnimEvents ────────────────────────────────

void registerAnimEvents(lua_State* L) {
	lua_newtable(L);

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* clipName = luaL_checkstring(Ls, 1);
		float time = (float)luaL_checknumber(Ls, 2);
		const char* eventName = luaL_checkstring(Ls, 3);
		const char* param = luaL_optstring(Ls, 4, "");
		anim::AnimationEventSystem::instance().addEvent(clipName, time, eventName, param);
		return 0;
	}); lua_setfield(L, -2, "add");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		luaL_checktype(Ls, 1, LUA_TFUNCTION);
		lua_pushvalue(Ls, 1);
		int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);

		anim::AnimationEventSystem::instance().setCallback(
			[ref, Ls](const std::string& name, const std::string& param) {
				lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
				lua_pushstring(Ls, name.c_str());
				lua_pushstring(Ls, param.c_str());
				if (lua_pcall(Ls, 2, 0, 0) != LUA_OK) {
					std::cerr << "[Lua] Anim event error: " << lua_tostring(Ls, -1) << std::endl;
					lua_pop(Ls, 1);
				}
			}
		);
		return 0;
	}); lua_setfield(L, -2, "set_callback");

	lua_pushcfunction(L, [](lua_State* Ls) -> int {
		const char* clipName = luaL_checkstring(Ls, 1);
		float currentTime = (float)luaL_checknumber(Ls, 2);
		float lastTime = (float)luaL_checknumber(Ls, 3);
		anim::AnimationEventSystem::instance().processClip(clipName, currentTime, lastTime);
		return 0;
	}); lua_setfield(L, -2, "process");

	lua_pushcfunction(L, [](lua_State*) -> int {
		anim::AnimationEventSystem::instance().clear();
		return 0;
	}); lua_setfield(L, -2, "clear");

	lua_setglobal(L, "anim_events");
}

} // namespace tucano::LuaBindings
