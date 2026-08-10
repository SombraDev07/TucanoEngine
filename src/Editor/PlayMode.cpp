#include "Editor/PlayMode.h"

#include "ECS/World.h"

namespace tucano::editor {

void PlayMode::bind(ecs::World* world, const ecs::SceneEnvironment& environment) {
	if (isPlaying()) stop();
	m_world = world;
	m_environment = environment;
}

bool PlayMode::play() {
	m_error.clear();
	if (isPlaying()) return true;
	if (m_world == nullptr) {
		m_error = "no world bound";
		return false;
	}

	m_snapshot = ecs::sceneToJson(*m_world, m_environment);
	if (m_snapshot.empty()) {
		// Without a snapshot, Stop would have nothing to put back and would silently keep whatever
		// the simulation did. Refusing to start is the only honest option.
		m_error = "could not snapshot the scene";
		return false;
	}

	m_state = State::Playing;
	m_playTime = 0.0f;
	if (onEnterPlay) onEnterPlay();
	return true;
}

void PlayMode::pause() {
	if (m_state == State::Playing) m_state = State::Paused;
}

void PlayMode::resume() {
	if (m_state == State::Paused) m_state = State::Playing;
}

void PlayMode::togglePause() {
	if (m_state == State::Playing) {
		m_state = State::Paused;
	} else if (m_state == State::Paused) {
		m_state = State::Playing;
	}
}

void PlayMode::stop() {
	if (!isPlaying()) return;

	// Leave play *before* restoring: the host tears down what it built from the running scene
	// (physics bodies, script state) while that scene still exists.
	if (onExitPlay) onExitPlay();

	m_state = State::Editing;
	m_playTime = 0.0f;

	if (m_world != nullptr && !m_snapshot.empty()) {
		// Errors here are reported, not thrown away: a restore that half-worked is exactly the case
		// where someone needs to know before they keep editing.
		ecs::sceneFromJson(m_snapshot, *m_world, m_environment, &m_error);
	}
	m_snapshot.clear();
}

void PlayMode::tick(float dt) {
	if (m_state != State::Playing) return;
	m_playTime += dt;
	if (onTick) onTick(dt);
}

} // namespace tucano::editor
