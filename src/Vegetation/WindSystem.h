#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <random>

namespace tucano::veg {

struct WindParams {
	float strength = 1.0f;
	float speed = 0.5f;
	float gustFrequency = 0.3f;
	float gustStrength = 2.0f;
	float turbulence = 0.4f;
	glm::vec3 direction{1, 0, 0};
};

struct WindInstanceData {
	glm::vec3 position;
	float height;
	float flexibility;
	float phase;
	glm::vec3 worldOffset; // GPU-computed per-frame
};

class WindSystem {
public:
	static WindSystem& instance() { static WindSystem ws; return ws; }

	void configure(const WindParams& p) { m_params = p; }
	WindParams& params() { return m_params; }

	void update(float dt) {
		m_time += dt;
		m_gustTimer += dt;

		if (m_gustTimer > m_gustCooldown) {
			m_currentGust = m_params.gustStrength * ((float(rand()) / RAND_MAX) * 2.0f - 1.0f);
			m_gustTimer = 0;
			m_gustCooldown = 0.5f + (float(rand()) / RAND_MAX) * 4.0f;
		}

		float gust = m_currentGust * smoothstep(m_gustTimer, 1.0f);
		m_effectiveStrength = m_params.strength * (1.0f + gust);

		updateDynamicEvents(dt);
	}

	glm::vec3 computeOffset(const glm::vec3& position, float height, float flexibility, float phase) const {
		float h = height * flexibility;
		glm::vec3 dir = glm::normalize(m_params.direction);
		float t = m_time * m_params.speed + phase;
		float noise = perlin(position.x * 0.1f + t, position.z * 0.1f) * m_params.turbulence;
		float wave = std::sin(position.x * 0.5f + t) * std::cos(position.z * 0.3f + t * 1.3f) * h * m_effectiveStrength;
		return dir * (wave + noise * h);
	}

	float effectiveStrength() const { return m_effectiveStrength; }
	float time() const { return m_time; }

	void triggerGust(float strength = 2.0f, float duration = 1.5f) {
		DynamicWindEvent evt;
		evt.type = DynamicWindEvent::Gust;
		evt.strength = strength;
		evt.duration = duration;
		evt.elapsed = 0;
		m_dynamicEvents.push_back(evt);
	}

	void triggerExplosion(const glm::vec3& epicenter, float strength = 5.0f, float radius = 20.0f) {
		DynamicWindEvent evt;
		evt.type = DynamicWindEvent::Explosion;
		evt.strength = strength;
		evt.radius = radius;
		evt.epicenter = epicenter;
		evt.duration = 3.0f;
		evt.elapsed = 0;
		m_dynamicEvents.push_back(evt);
	}

	void triggerHelicopter(const glm::vec3& position, float strength = 3.0f, float radius = 15.0f) {
		DynamicWindEvent evt;
		evt.type = DynamicWindEvent::Helicopter;
		evt.strength = strength;
		evt.radius = radius;
		evt.epicenter = position;
		evt.duration = 999.0f;
		evt.elapsed = 0;
		m_dynamicEvents.push_back(evt);
	}

	struct DynamicWindSample {
		float strength = 0;
		glm::vec3 direction{0, 0, 1};
	};

	DynamicWindSample sampleDynamicWind(const glm::vec3& position) const {
		DynamicWindSample sample;
		sample.strength = 0;
		sample.direction = glm::normalize(m_params.direction);

		for (auto& evt : m_dynamicEvents) {
			float dist = glm::distance(position, evt.epicenter);
			if (dist > evt.radius) continue;

			float timeFactor = 1.0f;
			if (evt.duration < 900.0f) {
				timeFactor = 1.0f - std::min(evt.elapsed / evt.duration, 1.0f);
				timeFactor = std::pow(timeFactor, 2.0f);
			}

			float distFactor = 1.0f - (dist / evt.radius);
			distFactor = std::pow(distFactor, 1.5f);

			float s = evt.strength * timeFactor * distFactor;
			if (evt.type == DynamicWindEvent::Explosion) {
				sample.direction = glm::normalize(position - evt.epicenter);
			} else if (evt.type == DynamicWindEvent::Helicopter) {
				glm::vec3 tangent(-position.z + evt.epicenter.z, 0, position.x - evt.epicenter.x);
				sample.direction = glm::normalize(tangent);
			}
			sample.strength += s;
		}

		return sample;
	}

	void updateDynamicEvents(float dt) {
		m_dynamicEvents.erase(std::remove_if(m_dynamicEvents.begin(), m_dynamicEvents.end(),
			[dt](DynamicWindEvent& evt) {
				evt.elapsed += dt;
				return evt.elapsed >= evt.duration && evt.duration < 900.0f;
			}), m_dynamicEvents.end());
	}

	void clearDynamicEvents() { m_dynamicEvents.clear(); }
	size_t dynamicEventCount() const { return m_dynamicEvents.size(); }

private:
	struct DynamicWindEvent {
		enum Type { Gust, Explosion, Helicopter } type = Gust;
		float strength = 1.0f;
		float duration = 1.0f;
		float elapsed = 0;
		float radius = 10.0f;
		glm::vec3 epicenter{0};
	};

	static float smoothstep(float t, float duration) {
		float x = std::clamp(t / duration, 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	}

	static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
	static float grad(int hash, float x) {
		int h = hash & 15;
		float grad = 1.0f + float(h & 7);
		if (h & 8) grad = -grad;
		return grad * x;
	}
	static float perlin(float x, float y) {
		int X = int(std::floor(x)) & 255;
		int Y = int(std::floor(y)) & 255;
		x -= std::floor(x);
		y -= std::floor(y);
		float u = fade(x), v = fade(y);

		static const int p[512] = {
			151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
			190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
			125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
			105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
			135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
			82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
			153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
			251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
			157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,
			66,215,61,156,180,
			151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
			190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
			125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
			105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
			135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
			82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
			153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
			251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
			157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,
			66,215,61,156,180
		};
		int a = p[p[X] + Y], b = p[p[X + 1] + Y];
		return glm::mix(grad(a, x), grad(b, x - 1), u);
	}

	WindParams m_params;
	float m_time = 0;
	float m_gustTimer = 0;
	float m_gustCooldown = 3.0f;
	float m_currentGust = 0;
	float m_effectiveStrength = 1.0f;
	mutable std::vector<DynamicWindEvent> m_dynamicEvents;
};

} // namespace tucano::veg
