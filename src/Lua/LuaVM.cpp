#include "Lua/LuaVM.h"
#include "Lua/LuaBindings.h"

#include "Core/Tween.h"
#include "Core/Coroutine.h"
#include "Core/EventBus.h"
#include "Renderer/CameraShake.h"
#include "Physics/TriggerVolume.h"
#include "Network/NetworkManager.h"
#include "Network/ReplicationSystem.h"
#include "Audio/SoundEvents.h"
#include "RHI/RHI.h"
#include "Renderer/Scene.h"

#include <fstream>
#include <iostream>
#include <windows.h>

static void luaLog(const std::string& msg) {
	OutputDebugStringA(("[Lua] " + msg + "\n").c_str());
	std::ofstream f("C:/TucanoEngine/build/windows-release/Samples/LuaLab/lua_engine.log", std::ios::app);
	if (f) f << msg << std::endl;
}

#include <iostream>
#include <fstream>
#include <sstream>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace tucano {

LuaVM& LuaVM::instance() {
	static LuaVM vm;
	return vm;
}

void LuaVM::init() {
	if (m_L) return;
	luaLog("[LuaVM] init() start");

	m_L = luaL_newstate();
	if (!m_L) { luaLog("[LuaVM] ERROR: luaL_newstate returned NULL"); return; }
	luaLog("[LuaVM] luaL_newstate OK");
	luaL_openlibs(m_L);
	luaLog("[LuaVM] luaL_openlibs OK");

	m_watchTimer = 0.0f;
	m_hotReload = false;

	TweenSystem::instance().setWorld(m_world);
}

void LuaVM::shutdown() {
	if (m_L) {
		lua_close(m_L);
		m_L = nullptr;
	}
	m_watchedFiles.clear();
	m_watchDirs.clear();
}

bool LuaVM::loadScript(const std::string& path) {
	if (!m_L) { luaLog("[LuaVM] loadScript: m_L is NULL"); return false; }
	luaLog("[LuaVM] loadScript: " + path);

	if (luaL_loadfile(m_L, path.c_str()) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		lua_pop(m_L, 1);
		std::cerr << "[Lua] Load error: " << m_lastError << std::endl;
		return false;
	}

	if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		lua_pop(m_L, 1);
		luaLog("[LuaVM] loadScript pcall error: " + m_lastError);
		return false;
	}

	luaLog("[LuaVM] loadScript OK: " + path);
	return true;
}

bool LuaVM::loadString(const std::string& code, const std::string& name) {
	if (!m_L) return false;

	if (luaL_loadbuffer(m_L, code.c_str(), code.size(), name.c_str()) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		lua_pop(m_L, 1);
		return false;
	}

	if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		lua_pop(m_L, 1);
		return false;
	}

	return true;
}

void LuaVM::call(const std::string& funcName) {
	if (!m_L) return;

	lua_getglobal(m_L, funcName.c_str());
	if (!lua_isfunction(m_L, -1)) {
		lua_pop(m_L, 1);
		return;
	}

	if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		std::cerr << "[Lua] Error calling '" << funcName << "': " << m_lastError << std::endl;
		lua_pop(m_L, 1);
	}
}

void LuaVM::callUpdate(float dt) {
	if (!m_L) return;

	lua_getglobal(m_L, "update");
	if (!lua_isfunction(m_L, -1)) {
		lua_pop(m_L, 1);
		return;
	}

	lua_pushnumber(m_L, dt);

	if (lua_pcall(m_L, 1, 0, 0) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		std::cerr << "[Lua] Error in update(): " << m_lastError << std::endl;
		lua_pop(m_L, 1);
	}
}

void LuaVM::callSetup() {
	if (!m_L || m_setupCalled) return;
	luaLog("[LuaVM] callSetup()");

	lua_getglobal(m_L, "setup");
	if (!lua_isfunction(m_L, -1)) {
		lua_pop(m_L, 1);
		std::cerr << "[Lua] No setup() function defined in script." << std::endl;
		return;
	}

	if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
		m_lastError = lua_tostring(m_L, -1);
		std::cerr << "[Lua] Error in setup(): " << m_lastError << std::endl;
		lua_pop(m_L, 1);
		return;
	}

	m_setupCalled = true;
	std::cout << "[Lua] setup() executed successfully." << std::endl;
}

void LuaVM::callUpdateFull(float dt) {
	LuaBindings::setTimeValues(dt);
	callUpdate(dt);
}

bool LuaVM::reload(const std::string& path) {
	return loadScript(path);
}

void LuaVM::watchDirectory(const std::string& dir, bool recursive) {
	m_watchDirs.push_back(dir);
	m_watchRecursive = recursive;
	collectScripts(dir);
}

void LuaVM::collectScripts(const std::string& dir) {
	try {
		if (m_watchRecursive) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
				if (entry.is_regular_file() && entry.path().extension() == ".lua") {
					auto path = entry.path().string();
					m_watchedFiles[path] = {entry.last_write_time(), path};
				}
			}
		} else {
			for (const auto& entry : std::filesystem::directory_iterator(dir)) {
				if (entry.is_regular_file() && entry.path().extension() == ".lua") {
					auto path = entry.path().string();
					m_watchedFiles[path] = {entry.last_write_time(), path};
				}
			}
		}
	} catch (const std::exception&) {}
}

void LuaVM::checkFileChanges() {
	for (const auto& dir : m_watchDirs) {
		try {
			auto scanDir = [this](const auto& self, const std::string& d) -> void {
				for (const auto& entry : std::filesystem::directory_iterator(d)) {
					if (entry.is_regular_file() && entry.path().extension() == ".lua") {
						auto path = entry.path().string();
						auto ftime = entry.last_write_time();
						auto it = m_watchedFiles.find(path);
						if (it == m_watchedFiles.end()) {
							m_watchedFiles[path] = {ftime, path};
							reload(path);
						} else if (ftime > it->second.lastWrite) {
							it->second.lastWrite = ftime;
							reload(path);
						}
					} else if (m_watchRecursive && entry.is_directory()) {
						self(self, entry.path().string());
					}
				}
			};
			scanDir(scanDir, dir);
		} catch (const std::exception&) {}
	}
}

void LuaVM::tick(float dt) {
	if (!m_L) return;

	CameraShake::instance().update(dt);
	TweenSystem::instance().update(dt);
	CoroutineSystem::instance().update(dt);
	physics::TriggerSystem::instance().update(dt);
	SoundEvents::instance().update();
	net::NetworkManager::instance().update(dt);
	net::ReplicationSystem::instance().tick();

	if (m_hotReload) {
		m_watchTimer += dt;
		if (m_watchTimer >= 0.5f) {
			m_watchTimer = 0.0f;
			checkFileChanges();
		}
	}

	LuaBindings::setTimeValues(dt);

	if (m_setupCalled) {
		callUpdate(dt);
	}

	if (m_setupCalled) {
		EventBus::instance().flush();
	}
}

void LuaVM::registerCoreBindings() {
	LuaBindings::registerLog(m_L);
	LuaBindings::registerTime(m_L);
	LuaBindings::registerMath(m_L);
	LuaBindings::registerECS(m_L, m_world);
	LuaBindings::registerInput(m_L, m_input);
	LuaBindings::registerEvents(m_L);
	LuaBindings::registerPool(m_L);
	LuaBindings::registerNetwork(m_L);
	LuaBindings::registerCoroutine(m_L);

	if (m_device) {
		lua_pushlightuserdata(m_L, m_device);
		lua_setfield(m_L, LUA_REGISTRYINDEX, "__tucano_device");
	}
	if (m_scene) {
		lua_pushlightuserdata(m_L, m_scene);
		lua_setfield(m_L, LUA_REGISTRYINDEX, "__tucano_scene");
	}
	if (m_renderer) {
		lua_pushlightuserdata(m_L, m_renderer);
		lua_setfield(m_L, LUA_REGISTRYINDEX, "__tucano_renderer");
	}

	LuaBindings::registerScenePrimitives(m_L);
	LuaBindings::registerRendererSettings(m_L);
}

void LuaVM::registerGameplayBindings() {
	LuaBindings::registerPhysics(m_L, m_physics, m_camera);
	LuaBindings::registerState(m_L);
	LuaBindings::registerTween(m_L);
	LuaBindings::registerAudio(m_L, m_audio);
	LuaBindings::registerAnimation(m_L);
	LuaBindings::registerTrigger(m_L);
	LuaBindings::registerCamera(m_L, m_camera);
	LuaBindings::registerSpline(m_L);
	LuaBindings::registerRPC(m_L);
	LuaBindings::registerReplication(m_L);
	LuaBindings::registerAnimController(m_L);
	LuaBindings::registerNavMesh(m_L);
	LuaBindings::registerBlendSpace(m_L);
	LuaBindings::registerAnimLayers(m_L);
	LuaBindings::registerAnimEvents(m_L);
	LuaBindings::registerVegetation(m_L);
	LuaBindings::registerBiome(m_L);
	LuaBindings::registerDensityMap(m_L);
	LuaBindings::registerExclusionZones(m_L);
	LuaBindings::registerInteraction(m_L);
	LuaBindings::registerSeason(m_L);
	LuaBindings::registerGrowth(m_L);
	LuaBindings::registerWindEvents(m_L);
}

void LuaVM::registerRenderBindings() {
	LuaBindings::registerRender2D(m_L, m_renderer);
}

void LuaVM::registerAllBindings() {
	registerCoreBindings();
	registerGameplayBindings();
	registerRenderBindings();
}

} // namespace tucano
