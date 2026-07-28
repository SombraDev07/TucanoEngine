#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_set>

namespace tucano::anim {

struct AnimationEvent {
	float time = 0;
	std::string name;
	std::string param;
};

struct AnimationEventState {
	float lastTime = 0;
	std::unordered_set<size_t> fired;
};

class AnimationEventSystem {
public:
	using EventCallback = std::function<void(const std::string& name, const std::string& param)>;

	static AnimationEventSystem& instance() { static AnimationEventSystem es; return es; }

	void setEventsForClip(const std::string& clipName, const std::vector<AnimationEvent>& events) {
		m_clipEvents[clipName] = events;
	}

	void addEvent(const std::string& clipName, float time, const std::string& name, const std::string& param = "") {
		m_clipEvents[clipName].push_back({time, name, param});
	}

	void setCallback(EventCallback cb) { m_callback = std::move(cb); }

	bool hasCallback() const { return m_callback != nullptr; }

	void processClip(const std::string& clipName, float currentTime, float lastTime) {
		auto it = m_clipEvents.find(clipName);
		if (it == m_clipEvents.end() || !m_callback) return;

		auto& state = m_clipStates[clipName];

		if (currentTime < lastTime) {
			state.fired.clear();
		}

		for (size_t i = 0; i < it->second.size(); ++i) {
			const auto& evt = it->second[i];
			if (state.fired.count(i)) continue;

			bool crossed = (lastTime <= evt.time && currentTime >= evt.time);
			if (crossed) {
				state.fired.insert(i);
				m_callback(evt.name, evt.param);
			}
		}

		state.lastTime = currentTime;
	}

	void clear() {
		m_clipEvents.clear();
		m_clipStates.clear();
	}

private:
	std::unordered_map<std::string, std::vector<AnimationEvent>> m_clipEvents;
	std::unordered_map<std::string, AnimationEventState> m_clipStates;
	EventCallback m_callback;
};

} // namespace tucano::anim
