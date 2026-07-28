#pragma once

#include "Audio/AudioClip.h"
#include "Audio/AudioSource.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {

class SoundEvents {
public:
	static SoundEvents& instance() {
		static SoundEvents s;
		return s;
	}

	void registerCue(const std::string& name, const std::string& filePath) {
		auto it = m_cues.find(name);
		if (it == m_cues.end()) {
			m_cues[name] = {filePath, nullptr};
		}
	}

	void play(const std::string& name) {
		auto it = m_cues.find(name);
		if (it == m_cues.end()) return;

		if (!it->second.clip) {
			it->second.clip.reset(AudioClip::loadWav(it->second.filePath.c_str()));
		}

		auto src = std::make_unique<AudioSource>();
		src->play(it->second.clip.get(), 1.0f, false);
		m_activeSources.push_back(std::move(src));
	}

	void play3D(const std::string& name, const glm::vec3& pos) {
		auto it = m_cues.find(name);
		if (it == m_cues.end()) return;

		if (!it->second.clip) {
			it->second.clip.reset(AudioClip::loadWav(it->second.filePath.c_str()));
		}

		auto src = std::make_unique<AudioSource>();
		src->setPosition(pos);
		src->play(it->second.clip.get(), 1.0f, false);
		m_activeSources.push_back(std::move(src));
	}

	void update() {
		m_activeSources.erase(
			std::remove_if(m_activeSources.begin(), m_activeSources.end(),
				[](const std::unique_ptr<AudioSource>& s) { return !s->isPlaying(); }),
			m_activeSources.end());
	}

private:
	struct Cue {
		std::string filePath;
		std::unique_ptr<AudioClip> clip;
	};

	std::unordered_map<std::string, Cue> m_cues;
	std::vector<std::unique_ptr<AudioSource>> m_activeSources;
};

} // namespace tucano
