#pragma once

#include <glm/glm.hpp>

#include <random>
#include <vector>

namespace tucano {

struct CameraShakeState {
	float intensity = 0.0f;
	float duration = 0.0f;
	float elapsed = 0.0f;
	float frequency = 10.0f;
	float roughness = 0.5f;
	float fadeIn = 0.05f;
	float fadeOut = 0.3f;

	glm::vec3 offset{0};
	glm::vec3 velocity{0};
	float seedX = 0;
	float seedY = 0;
	float seedZ = 0;
};

class CameraShake {
public:
	static CameraShake& instance() { static CameraShake cs; return cs; }

	void add(float intensity, float duration, float frequency = 10.0f, float roughness = 0.5f) {
		if (intensity <= 0 || duration <= 0) return;

		CameraShakeState s;
		s.intensity = intensity;
		s.duration = duration;
		s.elapsed = 0;
		s.frequency = frequency;
		s.roughness = roughness;
		s.fadeIn = 0.05f;
		s.fadeOut = 0.3f;
		s.seedX = randomFloat();
		s.seedY = randomFloat();
		s.seedZ = randomFloat();

		m_states.push_back(s);
	}

	void stop() {
		m_states.clear();
		m_totalOffset = {0,0,0};
	}

	void update(float dt) {
		m_totalOffset = {0,0,0};

		for (auto it = m_states.begin(); it != m_states.end(); ) {
			it->elapsed += dt;
			if (it->elapsed >= it->duration) {
				it = m_states.erase(it);
				continue;
			}

			float t = it->elapsed / it->duration;
			float fade = 1.0f;
			if (t < it->fadeIn / it->duration) fade = t / (it->fadeIn / it->duration);
			else if (t > 1.0f - it->fadeOut / it->duration) fade = (1.0f - t) / (it->fadeOut / it->duration);

			float currentIntensity = it->intensity * fade;

			float nx = perlin(it->elapsed * it->frequency + it->seedX, 0);
			float ny = perlin(it->elapsed * it->frequency + it->seedY, 1.3f);
			float nz = perlin(it->elapsed * it->frequency + it->seedZ, 2.7f);

			glm::vec3 target = glm::vec3(nx, ny, nz) * currentIntensity;
			it->offset = glm::mix(it->offset, target, it->roughness * 20.0f * dt);

			m_totalOffset += it->offset;
			++it;
		}
	}

	glm::vec3 offset() const { return m_totalOffset; }

private:
	static float randomFloat() {
		static std::mt19937 rng(42);
		static std::uniform_real_distribution<float> dist(0.0f, 100.0f);
		return dist(rng);
	}

	static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

	static float grad(int hash, float x, float y) {
		int h = hash & 3;
		float u = h < 2 ? x : y;
		float v = h < 2 ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
	}

	static float perlin(float x, float y) {
		int X = static_cast<int>(std::floor(x)) & 255;
		int Y = static_cast<int>(std::floor(y)) & 255;
		x -= std::floor(x);
		y -= std::floor(y);
		float u = fade(x);
		float v = fade(y);

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

		int a = p[p[X] + Y];
		int b = p[p[X + 1] + Y];
		int c = p[p[X] + Y + 1];
		int d = p[p[X + 1] + Y + 1];

		return glm::mix(glm::mix(grad(a, x, y), grad(b, x - 1, y), u),
		                glm::mix(grad(c, x, y - 1), grad(d, x - 1, y - 1), u), v);
	}

	std::vector<CameraShakeState> m_states;
	glm::vec3 m_totalOffset{0};
};

} // namespace tucano
