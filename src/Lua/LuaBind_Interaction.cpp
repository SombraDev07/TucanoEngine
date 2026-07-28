#include "Lua/LuaBindings.h"
#include "Vegetation/VegetationInteraction.h"
#include "Vegetation/SeasonSystem.h"
#include "Vegetation/GrowthSystem.h"
#include "Vegetation/WindSystem.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <glm/glm.hpp>

namespace tucano::LuaBindings {

void registerInteraction(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		float radius = (float)luaL_optnumber(Ls, 4, 2.0);
		float strength = (float)luaL_optnumber(Ls, 5, 1.0);
		veg::VegetationInteraction::instance().addInteractionPoint({x, y, z}, radius, strength);
		return 0;
	}, 0);
	lua_setfield(L, -2, "add_point");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		float dx = (float)luaL_checknumber(Ls, 4);
		float dy = (float)luaL_checknumber(Ls, 5);
		float dz = (float)luaL_checknumber(Ls, 6);
		float radius = (float)luaL_optnumber(Ls, 7, 10.0);
		float strength = (float)luaL_optnumber(Ls, 8, 3.0);
		float duration = (float)luaL_optnumber(Ls, 9, 1.0);
		veg::VegetationInteraction::instance().addForce({x, y, z}, {dx, dy, dz}, radius, strength, duration);
		return 0;
	}, 0);
	lua_setfield(L, -2, "add_force");

	lua_pushcclosure(L, [](lua_State*) -> int {
		veg::VegetationInteraction::instance().clearInteractionPoints();
		return 0;
	}, 0);
	lua_setfield(L, -2, "clear");

	lua_setglobal(L, "interaction");
}

void registerSeason(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		int season = (int)luaL_checkinteger(Ls, 1);
		if (season >= 0 && season < 4)
			veg::SeasonSystem::instance().setDayOfYear(float(season) * 90.0f + 45.0f);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_season");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float day = (float)luaL_checknumber(Ls, 1);
		veg::SeasonSystem::instance().setDayOfYear(day);
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_day");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		lua_pushinteger(Ls, int(veg::SeasonSystem::instance().currentSeason()));
		return 1;
	}, 0);
	lua_setfield(L, -2, "current");

	lua_setglobal(L, "season");
}

void registerGrowth(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t idx = (uint32_t)luaL_checkinteger(Ls, 1);
		uint32_t typeId = (uint32_t)luaL_checkinteger(Ls, 2);
		veg::GrowthSystem::instance().registerInstance(idx, typeId);
		return 0;
	}, 0);
	lua_setfield(L, -2, "register");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t idx = (uint32_t)luaL_checkinteger(Ls, 1);
		float scale = veg::GrowthSystem::instance().getScale(idx);
		lua_pushnumber(Ls, scale);
		return 1;
	}, 0);
	lua_setfield(L, -2, "scale");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t idx = (uint32_t)luaL_checkinteger(Ls, 1);
		veg::GrowthSystem::instance().removeInstance(idx);
		return 0;
	}, 0);
	lua_setfield(L, -2, "remove");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float rate = (float)luaL_optnumber(Ls, 1, 0.1);
		veg::GrowthSystem::instance().config().growthRate = rate;
		return 0;
	}, 0);
	lua_setfield(L, -2, "set_rate");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t idx = (uint32_t)luaL_checkinteger(Ls, 1);
		veg::DestructionSystem::instance().markDestroyed(idx);
		return 0;
	}, 0);
	lua_setfield(L, -2, "destroy");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t idx = (uint32_t)luaL_checkinteger(Ls, 1);
		lua_pushboolean(Ls, veg::DestructionSystem::instance().isDestroyed(idx));
		return 1;
	}, 0);
	lua_setfield(L, -2, "is_destroyed");

	lua_setglobal(L, "growth");
}

void registerWindEvents(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float str = (float)luaL_optnumber(Ls, 1, 2.0);
		float dur = (float)luaL_optnumber(Ls, 2, 1.5);
		veg::WindSystem::instance().triggerGust(str, dur);
		return 0;
	}, 0);
	lua_setfield(L, -2, "gust");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		float str = (float)luaL_optnumber(Ls, 4, 5.0);
		float rad = (float)luaL_optnumber(Ls, 5, 20.0);
		veg::WindSystem::instance().triggerExplosion({x, y, z}, str, rad);
		return 0;
	}, 0);
	lua_setfield(L, -2, "explosion");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float x = (float)luaL_checknumber(Ls, 1);
		float y = (float)luaL_checknumber(Ls, 2);
		float z = (float)luaL_checknumber(Ls, 3);
		veg::WindSystem::instance().triggerHelicopter({x, y, z});
		return 0;
	}, 0);
	lua_setfield(L, -2, "helicopter");

	lua_pushcclosure(L, [](lua_State*) -> int {
		veg::WindSystem::instance().clearDynamicEvents();
		return 0;
	}, 0);
	lua_setfield(L, -2, "clear");

	lua_setglobal(L, "wind_events");
}

} // namespace tucano::LuaBindings
