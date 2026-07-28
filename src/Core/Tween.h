#pragma once

#include "ECS/ComponentTypes.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {

inline float easeLinear(float t) { return t; }
inline float easeInQuad(float t) { return t * t; }
inline float easeOutQuad(float t) { return t * (2.0f - t); }
inline float easeInOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
inline float easeInCubic(float t) { return t * t * t; }
inline float easeOutCubic(float t) { t--; return t * t * t + 1.0f; }
inline float easeInOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f; }
inline float easeInElastic(float t) { return t == 0 ? 0 : t == 1 ? 1 : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * (2.0f * 3.14159265f) / 3.0f); }
inline float easeOutElastic(float t) { return t == 0 ? 0 : t == 1 ? 1 : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * 3.14159265f) / 3.0f) + 1.0f; }
inline float easeOutBounce(float t) { float n1=7.5625f,d1=2.75f;if(t<1/d1)return n1*t*t;if(t<2/d1){t-=1.5f/d1;return n1*t*t+0.75f;}if(t<2.5f/d1){t-=2.25f/d1;return n1*t*t+0.9375f;}t-=2.625f/d1;return n1*t*t+0.984375f; }

using EasingFunc = float(*)(float);

inline EasingFunc getEasing(const std::string& name) {
	if (name == "linear") return easeLinear;
	if (name == "easeInQuad") return easeInQuad;
	if (name == "easeOutQuad") return easeOutQuad;
	if (name == "easeInOutQuad") return easeInOutQuad;
	if (name == "easeInCubic") return easeInCubic;
	if (name == "easeOutCubic") return easeOutCubic;
	if (name == "easeInOutCubic") return easeInOutCubic;
	if (name == "easeInElastic") return easeInElastic;
	if (name == "easeOutElastic") return easeOutElastic;
	if (name == "easeOutBounce") return easeOutBounce;
	return easeLinear;
}

class TweenSystem {
public:
	static TweenSystem& instance() { static TweenSystem s; return s; }

	void setWorld(ecs::World* world) { m_world = world; }

	enum Type { Pos, Rot, Scale };
	struct Tween {
		ecs::Entity entity;
		Type type;
		float duration;
		float elapsed = 0;
		EasingFunc easing = easeLinear;
		glm::vec3 startPos;
		glm::vec3 endPos;
		glm::quat startRot{1,0,0,0};
		glm::quat endRot{1,0,0,0};
		glm::vec3 startScale{1};
		glm::vec3 endScale{1};
	};

	void tweenPosition(ecs::Entity e, glm::vec3 target, float duration, const std::string& easing) {
		auto* t = m_world ? m_world->get<ecs::TransformComponent>(e) : nullptr;
		glm::vec3 start = t ? t->position : glm::vec3(0);
		initTween(e, Pos, start, target, duration, easing, {1,0,0,0}, {1,0,0,0}, {1,1,1}, {1,1,1});
	}

	void tweenRotation(ecs::Entity e, glm::quat target, float duration, const std::string& easing) {
		auto* t = m_world ? m_world->get<ecs::TransformComponent>(e) : nullptr;
		glm::quat start = t ? t->rotation : glm::quat(1,0,0,0);
		initTween(e, Rot, {0,0,0}, {0,0,0}, duration, easing, start, target, {1,1,1}, {1,1,1});
	}

	void tweenScale(ecs::Entity e, glm::vec3 target, float duration, const std::string& easing) {
		auto* t = m_world ? m_world->get<ecs::TransformComponent>(e) : nullptr;
		glm::vec3 start = t ? t->scale : glm::vec3(1);
		initTween(e, Scale, {0,0,0}, {0,0,0}, duration, easing, {1,0,0,0}, {1,0,0,0}, start, target);
	}

	void initTween(ecs::Entity e, Type type,
	               glm::vec3 sPos, glm::vec3 ePos,
	               float dur, const std::string& easingName,
	               glm::quat sRot, glm::quat eRot,
	               glm::vec3 sScale, glm::vec3 eScale) {
		kill(e);
		m_tweens.push_back({e, type, dur, 0, getEasing(easingName), sPos, ePos, sRot, eRot, sScale, eScale});
	}

	void kill(ecs::Entity e) {
		m_tweens.erase(std::remove_if(m_tweens.begin(), m_tweens.end(),
			[e](const Tween& t) { return t.entity == e; }), m_tweens.end());
	}

	void killAll() { m_tweens.clear(); }

	void update(float dt) {
		for (auto it = m_tweens.begin(); it != m_tweens.end(); ) {
			it->elapsed += dt;
			float t = std::min(it->elapsed / it->duration, 1.0f);
			float v = it->easing(t);

			auto* comp = m_world ? m_world->get<ecs::TransformComponent>(it->entity) : nullptr;
			if (comp) {
				switch (it->type) {
					case Pos:   comp->position = glm::mix(it->startPos, it->endPos, v); break;
					case Rot:   comp->rotation = glm::slerp(it->startRot, it->endRot, v); break;
					case Scale: comp->scale = glm::mix(it->startScale, it->endScale, v); break;
				}
			}

			if (t >= 1.0f) {
				it = m_tweens.erase(it);
			} else {
				++it;
			}
		}
	}

private:
	ecs::World* m_world = nullptr;
	std::vector<Tween> m_tweens;
};

} // namespace tucano
