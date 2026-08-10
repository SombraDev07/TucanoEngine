#pragma once

// Play / Pause / Stop — running the game inside the editor without losing what you authored.
//
// I-01 of the roadmap, and step 10 of the Definition of Done. Until now "Play" flipped a bool and
// wrote a line to the console.
//
// The whole guarantee is this: **Stop puts the scene back exactly as it was before Play**. A bullet
// that moved, a crate that fell over, a script that renamed something — none of it survives. That
// is what makes it safe to press Play in the middle of authoring, and it is the reason this became
// cheap only after C-01: a snapshot is the scene serialised, and a restore is it loaded back.
//
// What actually *runs* during play is not decided here. The editor has no business hardcoding
// "physics plus Lua" — the host owns its simulation and supplies it through `onTick`. This class
// owns the state machine, the snapshot and the clock, which is the part that must not be
// reimplemented per host.
//
// Difference from I-02 (separate Editor World and Runtime World): that keeps two worlds alive at
// once and is the stronger design — a running game cannot touch authoring data at all. Snapshot and
// restore reaches the same *observable* guarantee against one world, for far less work. What it does
// not give is editing the authored scene while the game runs. Written down so the trade is a
// decision, not an oversight.

#include "ECS/SceneFile.h"

#include <functional>
#include <string>

namespace tucano::ecs {
class World;
}

namespace tucano::editor {

class PlayMode {
public:
	enum class State { Editing, Playing, Paused };

	// The world that gets snapshotted and restored, plus whatever environment blocks the host wants
	// included. Rebinding while playing stops first — leaving a snapshot pointing at a world that is
	// no longer bound would mean Stop restores into nothing.
	void bind(ecs::World* world, const ecs::SceneEnvironment& environment = {});

	// Snapshots and starts. Returns false when there is no world, or when the snapshot failed —
	// starting without one would make Stop destructive, which is the opposite of the point.
	bool play();

	// Keeps the state, stops advancing. `resume` undoes it; `togglePause` is what the toolbar
	// button and the keyboard shortcut both want.
	void pause();
	void resume();
	void togglePause();

	// Restores the snapshot and returns to Editing. Safe to call when not playing.
	void stop();

	State state() const { return m_state; }
	bool isPlaying() const { return m_state != State::Editing; }
	bool isPaused() const { return m_state == State::Paused; }

	// Advances the simulation by `dt` when playing and not paused. Call once per frame; it is a
	// no-op in every other state, so the host does not need to branch.
	void tick(float dt);

	// Seconds since Play, not counting time spent paused. This is game time, so a script that reads
	// it does not see the pause as a jump.
	float playTime() const { return m_playTime; }

	// What the host runs while playing: physics steps, scripts, animation. Never called while
	// paused or editing.
	std::function<void(float)> onTick;

	// Called on entering and leaving play, for the host to set up and tear down whatever the
	// snapshot cannot carry — a physics world rebuilt from the entities, say.
	std::function<void()> onEnterPlay;
	std::function<void()> onExitPlay;

	const std::string& error() const { return m_error; }

	// The serialised scene taken at Play, empty when not playing. Exposed so a test can look at it
	// rather than inferring it from behaviour.
	const std::string& snapshot() const { return m_snapshot; }

private:
	ecs::World* m_world = nullptr;
	ecs::SceneEnvironment m_environment;
	State m_state = State::Editing;
	std::string m_snapshot;
	std::string m_error;
	float m_playTime = 0.0f;
};

} // namespace tucano::editor
