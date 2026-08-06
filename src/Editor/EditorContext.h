#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

class Scene;
class Renderer;
class Camera;
struct RendererSettings;

namespace editor {

struct LogEntry {
	enum class Level : uint8_t { Info, Warning, Error };
	Level level = Level::Info;
	std::string message;
};

struct EditorContext {
	Scene* scene = nullptr;
	Renderer* renderer = nullptr;
	RendererSettings* settings = nullptr;
	Camera* camera = nullptr;

	// Selection
	int selectedObject = -1;

	// Performance stats (set each frame by host)
	float frameMs = 0.0f;
	uint32_t drawCalls = 0;
	float simMs = 0.0f;
	float vegMs = 0.0f;
	float renderMs = 0.0f;
	float uiMs = 0.0f;
	uint32_t viewportW = 1920;
	uint32_t viewportH = 1080;

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
