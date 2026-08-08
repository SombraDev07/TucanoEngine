#include "Lua/LuaBindings.h"
#include "Renderer/Renderer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include "Renderer/DevTexture.h"
#include "Renderer/Scene.h"
#include "RHI/RHI.h"
#include "Core/Coroutine.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <glm/glm.hpp>

namespace tucano::LuaBindings {

static rhi::Device* getDevice(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_device");
	auto* d = static_cast<rhi::Device*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return d;
}

static Scene* getScene(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_scene");
	auto* s = static_cast<Scene*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return s;
}

static Renderer* getRenderer(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__tucano_renderer");
	auto* r = static_cast<Renderer*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return r;
}

static std::shared_ptr<Mesh> s_cachedCubeMesh;

static std::shared_ptr<Mesh> getOrCreateCubeMesh(rhi::Device& device) {
	if (s_cachedCubeMesh) return s_cachedCubeMesh;

	const float s = 0.5f;
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;

	auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
		uint32_t base = uint32_t(verts.size());
		verts.push_back({p0, n, {t.x,t.y,t.z,1}, {1,1}, {1,1,1,1}});
		verts.push_back({p1, n, {t.x,t.y,t.z,1}, {0,1}, {1,1,1,1}});
		verts.push_back({p2, n, {t.x,t.y,t.z,1}, {0,0}, {1,1,1,1}});
		verts.push_back({p3, n, {t.x,t.y,t.z,1}, {1,0}, {1,1,1,1}});
		indices.insert(indices.end(), {base, base+1, base+2, base, base+2, base+3});
	};

	face({ 0, 1, 0},{ 1, 0, 0}, {-s, s,-s},{ s, s,-s},{ s, s, s},{-s, s, s});
	face({ 0,-1, 0},{ 1, 0, 0}, {-s,-s, s},{ s,-s, s},{ s,-s,-s},{-s,-s,-s});
	face({ 0, 0, 1},{ 1, 0, 0}, {-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s});
	face({ 0, 0,-1},{-1, 0, 0}, { s,-s,-s},{-s,-s,-s},{-s, s,-s},{ s, s,-s});
	face({ 1, 0, 0},{ 0, 0,-1}, { s,-s, s},{ s,-s,-s},{ s, s,-s},{ s, s, s});
	face({-1, 0, 0},{ 0, 0, 1}, {-s,-s,-s},{-s,-s, s},{-s, s, s},{-s, s,-s});

	SubMesh sub;
	sub.indexCount = uint32_t(indices.size());
	sub.aabbMin = {-s,-s,-s};
	sub.aabbMax = { s, s, s};

	s_cachedCubeMesh = Mesh::create(device, verts, indices, {sub});
	// The cache outlives the device unless it is told not to: a static shared_ptr<Mesh> is freed
	// during static destruction, long after the device it was built from is gone. Registered here
	// rather than at startup so a program that never creates a cube never registers anything.
	device.onBeforeDestroy([] { releaseCachedResources(); });
	return s_cachedCubeMesh;
}

void registerScenePrimitives(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* device = getDevice(Ls);
		auto* scene = getScene(Ls);
		if (!device || !scene) { lua_pushinteger(Ls, 0); return 1; }

		float x = (float)luaL_optnumber(Ls, 1, 0.0);
		float y = (float)luaL_optnumber(Ls, 2, 0.0);
		float z = (float)luaL_optnumber(Ls, 3, 0.0);
		float scale = (float)luaL_optnumber(Ls, 4, 1.0);
		if (scale <= 0) scale = 1.0f;

		auto mesh = getOrCreateCubeMesh(*device);
		if (!mesh) { lua_pushinteger(Ls, 0); return 1; }

		auto mat = std::make_shared<Material>();
		mat->name = "Cube";
		mat->baseColorFactor = {1,1,1,1};
		mat->albedo = devtex::defaultAlbedo(*device);
		mat->normal = devtex::defaultNormal(*device);
		mat->roughnessFactor = 0.72f;
		mat->metallicFactor = 0.0f;

		RenderObject obj;
		obj.name = "Cube" + std::to_string(scene->objects.size());
		obj.mesh = mesh;
		obj.materials = {mat};
		obj.transform.translation = {x, y, z};
		obj.transform.scale = glm::vec3(scale);
		obj.worldMatrix = obj.transform.matrix();

		uint32_t idx = uint32_t(scene->objects.size());
		scene->objects.push_back(std::move(obj));
		lua_pushinteger(Ls, idx + 1);
		return 1;
	}, 0);
	lua_setfield(L, -2, "spawn_cube");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* scene = getScene(Ls);
		lua_pushinteger(Ls, scene ? uint32_t(scene->objects.size()) : 0);
		return 1;
	}, 0);
	lua_setfield(L, -2, "object_count");

	lua_setglobal(L, "scene");
}

void registerRendererSettings(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* r = getRenderer(Ls);
		if (r) r->settings().timeOfDay = (float)luaL_checknumber(Ls, 1);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_time_of_day");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* r = getRenderer(Ls);
		if (r) { r->rain().enabled = lua_toboolean(Ls, 1); r->rain().amount = (float)luaL_optnumber(Ls, 2, 1.0); }
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_rain");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* r = getRenderer(Ls);
		if (r) r->settings().enableAtmosphere = lua_toboolean(Ls, 1);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_atmosphere");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* r = getRenderer(Ls);
		if (r) r->settings().enableClouds = lua_toboolean(Ls, 1);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_clouds");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* r = getRenderer(Ls);
		if (r) r->settings().cloudCoverage = (float)luaL_checknumber(Ls, 1);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_cloud_coverage");

	lua_setglobal(L, "renderer");
}

void releaseCachedResources() {
	s_cachedCubeMesh.reset();
}

} // namespace tucano::LuaBindings
