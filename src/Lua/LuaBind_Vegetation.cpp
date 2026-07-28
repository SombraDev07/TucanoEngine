#include "Lua/LuaBindings.h"
#include "Vegetation/VegetationSystem.h"
#include "Vegetation/WindSystem.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace tucano::LuaBindings {

void registerVegetation(lua_State* L) {
	lua_newtable(L);

	// Types
	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		const char* name = luaL_checkstring(Ls, 1);
		veg::VegetationType t;
		t.name = name;
		t.minScale = (float)luaL_optnumber(Ls, 2, 0.7);
		t.maxScale = (float)luaL_optnumber(Ls, 3, 1.3);
		t.windFlexibility = (float)luaL_optnumber(Ls, 4, 0.5);
		t.cullDistance = (float)luaL_optnumber(Ls, 5, 150.0);
		if (lua_isstring(Ls, 6)) t.meshPath = lua_tostring(Ls, 6);
		t.proceduralKind = (uint32_t)luaL_optinteger(Ls, 7, 0);
		uint32_t id = veg::VegetationSystem::instance().registerType(t);
		if (!t.meshPath.empty())
			veg::VegetationSystem::instance().paint().meshDirty = true;
		lua_pushinteger(Ls, id);
		return 1;
	}, 0);
	lua_setfield(L, -2, "register_type");

	// Scatter
	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		int cx = (int)luaL_checkinteger(Ls, 1);
		int cz = (int)luaL_checkinteger(Ls, 2);
		uint32_t typeId = (uint32_t)luaL_checkinteger(Ls, 3);
		uint32_t count = (uint32_t)luaL_checkinteger(Ls, 4);
		uint32_t seed = (uint32_t)luaL_optinteger(Ls, 5, 42);
		veg::VegetationSystem::instance().scatter(cx, cz, typeId, count, seed);
		return 0;
	}, 0);
	lua_setfield(L, -2, "scatter");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		uint32_t typeId = (uint32_t)luaL_checkinteger(Ls, 4);
		uint32_t id = veg::VegetationSystem::instance().placeAt({x, y, z}, typeId);
		lua_pushinteger(Ls, id);
		return 1;
	}, 0);
	lua_setfield(L, -2, "place");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		float radius = (float)luaL_checknumber(Ls, 4);
		uint32_t typeId = (uint32_t)luaL_checkinteger(Ls, 5);
		float strength = (float)luaL_optnumber(Ls, 6, 0.6);
		float dens = (float)luaL_optnumber(Ls, 7, 2.0);
		uint32_t n = veg::VegetationSystem::instance().paintBrush(
		    {x, y, z}, radius, typeId, strength, dens);
		lua_pushinteger(Ls, n);
		return 1;
	}, 0);
	lua_setfield(L, -2, "paint");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		float radius = (float)luaL_checknumber(Ls, 4);
		uint32_t n = veg::VegetationSystem::instance().eraseBrush({x, y, z}, radius);
		lua_pushinteger(Ls, n);
		return 1;
	}, 0);
	lua_setfield(L, -2, "erase");

	// Clear
	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		int cx = (int)luaL_checkinteger(Ls, 1);
		int cz = (int)luaL_checkinteger(Ls, 2);
		veg::VegetationSystem::instance().removeInstances(cx, cz);
		return 0;
	}, 0);
	lua_setfield(L, -2, "clear_cell");

	lua_pushcclosure(L, [](lua_State*) -> int {
		veg::VegetationSystem::instance().clear();
		return 0;
	}, 0);
	lua_setfield(L, -2, "clear_all");

	// Stats
	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		lua_pushinteger(Ls, veg::VegetationSystem::instance().instanceCount());
		return 1;
	}, 0);
	lua_setfield(L, -2, "instance_count");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		lua_pushinteger(Ls, veg::VegetationSystem::instance().typeCount());
		return 1;
	}, 0);
	lua_setfield(L, -2, "type_count");

	// Wind
	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float str = (float)luaL_optnumber(Ls, 1, 1.0);
		float speed = (float)luaL_optnumber(Ls, 2, 0.5);
		veg::WindParams p;
		p.strength = str;
		p.speed = speed;
		veg::WindSystem::instance().configure(p);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_wind");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		lua_pushnumber(Ls, veg::WindSystem::instance().effectiveStrength());
		return 1;
	}, 0);
	lua_setfield(L, -2, "wind_strength");

	lua_setglobal(L, "veg");
}

} // namespace tucano::LuaBindings
