#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

namespace tucano::veg {

enum class Season : uint8_t {
	Spring = 0,
	Summer = 1,
	Autumn = 2,
	Winter = 3,
	Count = 4
};

struct SeasonColors {
	glm::vec3 baseColor{1, 1, 1};
	glm::vec3 tipColor{1, 1, 1};
	float colorVariation = 0.2f;
	float leafDrop = 0;
	float snowAmount = 0;
};

struct SeasonTransition {
	Season from = Season::Summer;
	Season to = Season::Autumn;
	float progress = 0;
	float speed = 0.1f;
};

struct SeasonConfig {
	Season currentSeason = Season::Summer;
	float yearLength = 360.0f;
	float dayOfYear = 180.0f;
	float transitionDuration = 15.0f;
	bool autoAdvance = true;
	float timeScale = 1.0f;
};

class SeasonSystem {
public:
	static SeasonSystem& instance() { static SeasonSystem ss; return ss; }

	void configure(const SeasonConfig& cfg) { m_config = cfg; }
	SeasonConfig& config() { return m_config; }

	void setSeasonColors(Season season, uint32_t typeId, const SeasonColors& colors) {
		m_typeColors[typeId][int(season)] = colors;
	}

	void setSeasonColors(Season season, const std::string& typeName, const SeasonColors& colors);

	SeasonColors getColors(Season season, uint32_t typeId) const {
		auto it = m_typeColors.find(typeId);
		if (it != m_typeColors.end()) return it->second[int(season)];

		switch (season) {
			case Season::Spring: return {{0.3f, 0.9f, 0.3f}, {0.5f, 1.0f, 0.4f}, 0.3f, 0, 0};
			case Season::Summer: return {{0.1f, 0.7f, 0.2f}, {0.2f, 0.8f, 0.3f}, 0.2f, 0, 0};
			case Season::Autumn: return {{0.8f, 0.4f, 0.1f}, {0.9f, 0.3f, 0.1f}, 0.4f, 0.3f, 0};
			case Season::Winter: return {{0.5f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.7f}, 0.1f, 0.8f, 0.4f};
			default: return {{1, 1, 1}, {1, 1, 1}, 0, 0, 0};
		}
	}

	glm::vec3 blendColor(Season from, Season to, float t, uint32_t typeId, bool isTip) const {
		auto cFrom = getColors(from, typeId);
		auto cTo = getColors(to, typeId);
		glm::vec3 base = glm::mix(isTip ? cFrom.tipColor : cFrom.baseColor,
		                          isTip ? cTo.tipColor : cTo.baseColor, t);

		float variation = glm::mix(cFrom.colorVariation, cTo.colorVariation, t);
		float noise = std::sin(float(typeId) * 12.9898f) * 0.5f + 0.5f;
		base += glm::vec3(variation) * (noise - 0.5f) * 2.0f;

		return glm::clamp(base, glm::vec3(0), glm::vec3(1));
	}

	float getSnowAmount(uint32_t typeId) const {
		if (m_config.currentSeason == Season::Winter)
			return getColors(Season::Winter, typeId).snowAmount;
		return 0;
	}

	float getLeafDrop(uint32_t typeId) const {
		if (m_config.currentSeason == Season::Autumn)
			return getColors(Season::Autumn, typeId).leafDrop;
		return 0;
	}

	Season currentSeason() const { return m_config.currentSeason; }

	void update(float dt) {
		if (!m_config.autoAdvance) return;

		m_config.dayOfYear += dt * m_config.timeScale;
		while (m_config.dayOfYear >= m_config.yearLength) m_config.dayOfYear -= m_config.yearLength;
		while (m_config.dayOfYear < 0) m_config.dayOfYear += m_config.yearLength;

		float seasonLength = m_config.yearLength / 4.0f;
		int seasonIndex = int(m_config.dayOfYear / seasonLength);
		m_config.currentSeason = Season(std::clamp(seasonIndex, 0, 3));
	}

	void setDayOfYear(float day) {
		m_config.dayOfYear = day;
		float seasonLength = m_config.yearLength / 4.0f;
		m_config.currentSeason = Season(std::min(int(day / seasonLength), 3));
	}

	float seasonProgress() const {
		float seasonLength = m_config.yearLength / 4.0f;
		float dayInSeason = std::fmod(m_config.dayOfYear, seasonLength);
		return dayInSeason / seasonLength;
	}

private:
	SeasonConfig m_config;
	std::unordered_map<uint32_t, SeasonColors[4]> m_typeColors;
};

} // namespace tucano::veg
