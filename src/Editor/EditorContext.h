#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tucano {

namespace terrain {
struct TerrainGenParams;
}

class Scene;
class Renderer;
class Camera;
struct RendererSettings;

namespace ecs {
class World;
}

namespace asset {
class AssetRegistry;
}

namespace editor {

class UndoStack;
class PlayMode;

struct LogEntry {
	enum class Level : uint8_t { Info, Warning, Error };
	Level level = Level::Info;
	std::string message;
};

struct EditorContext {
	// What the editor authors (C-02): entities with components. `world` is the source of truth;
	// `scene` is render data derived from it by ecs::syncTransformsToScene.
	ecs::World* world = nullptr;
	Scene* scene = nullptr;
	Renderer* renderer = nullptr;
	RendererSettings* settings = nullptr;
	Camera* camera = nullptr;

	// Selection. `selectedEntity` is the real one; `selectedObject` is the pre-C-02 index into
	// Scene::objects, kept working while hosts that have no World still exist. Both are honoured,
	// and the entity path wins when a world is bound.
	uint32_t selectedEntity = 0xFFFFFFFFu;
	int selectedObject = -1;

	// Where scene commands record their steps. Null is tolerated — the operation still happens, it
	// just cannot be undone, which is what a host without a workspace gets.
	UndoStack* undo = nullptr;

	// Play/Pause/Stop. Null when the host has no play mode, which is what the panels check before
	// offering the buttons.
	PlayMode* play = nullptr;

	// The project's assets, indexed by GUID (B-01). Null when no project root has been scanned,
	// which is what the Content Browser and the Pickers check before offering anything.
	asset::AssetRegistry* assets = nullptr;
	// Root the registry's relative paths are against, so a panel can turn one back into a file.
	std::string assetRoot;

	bool hasSelectedEntity() const { return world != nullptr && selectedEntity != 0xFFFFFFFFu; }
	void clearSelection() {
		selectedEntity = 0xFFFFFFFFu;
		selectedObject = -1;
	}

	// Performance stats (set each frame by host)
	float frameMs = 0.0f;
	uint32_t drawCalls = 0;
	float simMs = 0.0f;
	float vegMs = 0.0f;
	float renderMs = 0.0f;
	float uiMs = 0.0f;
	uint32_t viewportW = 1920;
	uint32_t viewportH = 1080;

	// ── Viewport panel ──
	//
	// The scene is rendered into an offscreen target and shown as an image, because the editor
	// cannot show the world *behind* its panels: the tool window is docked into the shell's central
	// node, and an occupied dock node paints over everything drawn before ImGui. This is also what
	// the gizmo and click-picking need — both work in coordinates relative to the panel, not the
	// window.
	//
	// The host writes `sceneTexture` (an ImTextureID) each frame; the panel writes back the size it
	// wants, and the host resizes the target and the camera's aspect before the next render.
	uint64_t sceneTexture = 0;
	uint32_t requestedViewportW = 0;
	uint32_t requestedViewportH = 0;
	// True while the cursor is over the rendered image. The host needs it because `WantCaptureMouse`
	// is true over *any* ImGui window, and the viewport is one now — without this the camera stops
	// responding the moment the scene moves into a panel.
	bool viewportHovered = false;

	// ── Terrain (F-01) ──
	//
	// Published by the host, because building a terrain needs a device, a physics world and a place
	// in the render scene — none of which the editor owns. The panel edits `terrainParams` freely
	// (it is just numbers) and calls `applyTerrain` when somebody asks for the landscape to be
	// rebuilt; the scene file does the same on load. Null when the host has no terrain support,
	// which is what the panel checks before offering anything.
	terrain::TerrainGenParams* terrainParams = nullptr;
	std::function<bool(const terrain::TerrainGenParams&)> applyTerrain;

	// Console log
	static constexpr size_t kMaxLogEntries = 512;
	std::vector<LogEntry> log;

	void logInfo(const std::string& msg) { addLog(LogEntry::Level::Info, msg); }
	void logWarn(const std::string& msg) { addLog(LogEntry::Level::Warning, msg); }
	void logError(const std::string& msg) { addLog(LogEntry::Level::Error, msg); }
	void clearLog() { log.clear(); }

private:
	void addLog(LogEntry::Level level, const std::string& msg) {
		if (log.size() >= kMaxLogEntries) {
			log.erase(log.begin());
		}
		log.push_back({level, msg});
	}
};

} // namespace editor
} // namespace tucano
