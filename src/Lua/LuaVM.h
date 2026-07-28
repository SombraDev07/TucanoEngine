#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

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
struct RendererSettings;

class LuaVM {
public:
	static LuaVM& instance();

	void init();
	void shutdown();
	bool isInitialized() const { return m_L != nullptr; }

	void setWorld(ecs::World* world) { m_world = world; }
	void setPhysics(physics::PhysicsWorld* phys) { m_physics = phys; }
	void setRenderer(Renderer* renderer) { m_renderer = renderer; }
	void setCamera(Camera* camera) { m_camera = camera; }
	void setAudio(Audio* audio) { m_audio = audio; }
	void setInput(Input* input) { m_input = input; }
	void setDevice(rhi::Device* device) { m_device = device; }
	void setScene(Scene* scene) { m_scene = scene; }

	ecs::World* world() const { return m_world; }
	physics::PhysicsWorld* physics() const { return m_physics; }
	Renderer* renderer() const { return m_renderer; }
	Camera* camera() const { return m_camera; }
	Audio* audio() const { return m_audio; }
	Input* input() const { return m_input; }
	rhi::Device* device() const { return m_device; }
	Scene* scene() const { return m_scene; }

	bool loadScript(const std::string& path);
	bool loadString(const std::string& code, const std::string& name = "inline");

	void call(const std::string& funcName);
	void callUpdate(float dt);

	void callSetup();
	void callUpdateFull(float dt);

	bool reload(const std::string& path);

	void watchDirectory(const std::string& dir, bool recursive = true);
	void enableHotReload(bool enable) { m_hotReload = enable; }

	void tick(float dt);

	std::string lastError() const { return m_lastError; }
	lua_State* state() { return m_L; }

	bool setupCalled() const { return m_setupCalled; }

	void registerCoreBindings();
	void registerGameplayBindings();
	void registerRenderBindings();
	void registerAllBindings();

private:
	LuaVM() = default;
	~LuaVM() { shutdown(); }

	void collectScripts(const std::string& dir);
	void checkFileChanges();

	lua_State* m_L = nullptr;
	std::string m_lastError;

	ecs::World* m_world = nullptr;
	physics::PhysicsWorld* m_physics = nullptr;
	Renderer* m_renderer = nullptr;
	Camera* m_camera = nullptr;
	Audio* m_audio = nullptr;
	Input* m_input = nullptr;
	rhi::Device* m_device = nullptr;
	Scene* m_scene = nullptr;

	bool m_hotReload = false;
	float m_watchTimer = 0.0f;
	bool m_setupCalled = false;

	struct WatchedFile {
		std::filesystem::file_time_type lastWrite;
		std::string path;
	};
	std::vector<std::string> m_watchDirs;
	bool m_watchRecursive = true;
	std::unordered_map<std::string, WatchedFile> m_watchedFiles;
};

} // namespace tucano
