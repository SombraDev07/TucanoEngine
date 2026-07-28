#include "Lua/LuaBindings.h"
#include "Vegetation/BiomeSystem.h"
#include "Vegetation/ProceduralPlacement.h"
#include "Vegetation/DensityMap.h"
#include "Vegetation/VegetationSystem.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace tucano::LuaBindings {

void registerBiome(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		const char* name = luaL_checkstring(Ls, 1);
		veg::BiomeLayer layer;
		layer.name = name;
		layer.priority = (float)luaL_optnumber(Ls, 2, 0);

		if (lua_istable(Ls, 3)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 3)) {
				veg::BiomeRule rule;
				lua_getfield(Ls, -1, "condition"); rule.condition = static_cast<veg::BiomeRule::Condition>((int)lua_tointeger(Ls, -1)); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "valueA"); rule.valueA = (float)luaL_optnumber(Ls, -1, 0); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "valueB"); rule.valueB = (float)luaL_optnumber(Ls, -1, 0); lua_pop(Ls, 1);
				lua_getfield(Ls, -1, "weight"); rule.weight = (float)luaL_optnumber(Ls, -1, 1.0); lua_pop(Ls, 1);
				layer.rules.push_back(rule);
				lua_pop(Ls, 1);
			}
		}

		if (lua_istable(Ls, 4)) {
			lua_pushnil(Ls);
			while (lua_next(Ls, 4)) {
				lua_rawgeti(Ls, -1, 1); uint32_t typeId = (uint32_t)lua_tointeger(Ls, -1); lua_pop(Ls, 1);
				lua_rawgeti(Ls, -1, 2); float prob = (float)luaL_optnumber(Ls, -1, 1.0); lua_pop(Ls, 1);
				layer.vegetationTypes.push_back({typeId, prob});
				lua_pop(Ls, 1);
			}
		}

		layer.density = (float)luaL_optnumber(Ls, 5, 1.0);
		layer.clusterSize = (float)luaL_optnumber(Ls, 6, 0);
		veg::BiomeSystem::instance().addLayer(layer);
		return 0;
	}, 0);
	lua_setfield(L, -2, "add_layer");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		const char* name = luaL_checkstring(Ls, 1);
		veg::BiomeSystem::instance().removeLayer(name);
		return 0;
	}, 0);
	lua_setfield(L, -2, "remove_layer");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		lua_pushinteger(Ls, veg::BiomeSystem::instance().layerCount());
		return 1;
	}, 0);
	lua_setfield(L, -2, "layer_count");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		int cx = (int)luaL_checkinteger(Ls, 1);
		int cz = (int)luaL_checkinteger(Ls, 2);
		veg::VegetationSystem::instance().scatterBiome(cx, cz, veg::BiomeSystem::instance(), veg::ScatterConfig{});
		return 0;
	}, 0);
	lua_setfield(L, -2, "scatter_cell");

	lua_pushcclosure(L, [](lua_State*) -> int {
		veg::BiomeSystem::instance().clear();
		return 0;
	}, 0);
	lua_setfield(L, -2, "clear");

	lua_setglobal(L, "biome");
}

void registerDensityMap(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		uint32_t w = (uint32_t)luaL_checkinteger(Ls, 1);
		uint32_t h = (uint32_t)luaL_checkinteger(Ls, 2);
		float def = (float)luaL_optnumber(Ls, 3, 1.0);
		auto* map = new veg::DensityMap();
		map->create(w, h, def);
		lua_pushlightuserdata(Ls, map);
		return 1;
	}, 0);
	lua_setfield(L, -2, "new");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* map = static_cast<veg::DensityMap*>(lua_touserdata(Ls, 1));
		float wx = (float)luaL_checknumber(Ls, 2);
		float wz = (float)luaL_checknumber(Ls, 3);
		float r = (float)luaL_checknumber(Ls, 4);
		float s = (float)luaL_checknumber(Ls, 5);
		if (map) map->paintBrush(wx, wz, r, s);
		return 0;
	}, 0);
	lua_setfield(L, -2, "paint");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* map = static_cast<veg::DensityMap*>(lua_touserdata(Ls, 1));
		float wx = (float)luaL_checknumber(Ls, 2);
		float wz = (float)luaL_checknumber(Ls, 3);
		lua_pushnumber(Ls, map ? map->sample(wx, wz) : 1.0);
		return 1;
	}, 0);
	lua_setfield(L, -2, "sample");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* map = static_cast<veg::DensityMap*>(lua_touserdata(Ls, 1));
		const char* path = luaL_checkstring(Ls, 2);
		lua_pushboolean(Ls, map ? map->save(path) : false);
		return 1;
	}, 0);
	lua_setfield(L, -2, "save");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* map = static_cast<veg::DensityMap*>(lua_touserdata(Ls, 1));
		const char* path = luaL_checkstring(Ls, 2);
		lua_pushboolean(Ls, map ? map->load(path) : false);
		return 1;
	}, 0);
	lua_setfield(L, -2, "load");

	lua_setglobal(L, "density_map");
}

void registerExclusionZones(lua_State* L) {
	lua_newtable(L);

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float cx = (float)luaL_checknumber(Ls, 1);
		float cz = (float)luaL_checknumber(Ls, 2);
		float hw = (float)luaL_checknumber(Ls, 3);
		float hd = (float)luaL_checknumber(Ls, 4);
		auto* zones = new veg::ExclusionZone();
		zones->addRectangle({cx, cz}, {hw, hd});
		lua_pushlightuserdata(Ls, zones);
		return 1;
	}, 0);
	lua_setfield(L, -2, "new_rect");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		float cx = (float)luaL_checknumber(Ls, 1);
		float cz = (float)luaL_checknumber(Ls, 2);
		float r = (float)luaL_checknumber(Ls, 3);
		auto* zones = new veg::ExclusionZone();
		zones->addCircle({cx, cz}, r);
		lua_pushlightuserdata(Ls, zones);
		return 1;
	}, 0);
	lua_setfield(L, -2, "new_circle");

	lua_pushcclosure(L, [](lua_State* Ls) -> int {
		auto* zones = static_cast<veg::ExclusionZone*>(lua_touserdata(Ls, 1));
		float wx = (float)luaL_checknumber(Ls, 2);
		float wz = (float)luaL_checknumber(Ls, 3);
		lua_pushboolean(Ls, zones ? zones->isExcluded(wx, wz) : false);
		return 1;
	}, 0);
	lua_setfield(L, -2, "check");

	lua_setglobal(L, "exclusion");
}

} // namespace tucano::LuaBindings
