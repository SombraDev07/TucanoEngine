#pragma once

#include "Animation/AnimationClip.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::anim {

struct AnimCondition {
	std::string paramName;
	enum Op { Equals, NotEquals, Greater, Less, GreaterEq, LessEq, BoolTrue, BoolFalse } op = Equals;
	float value = 0.0f;
	bool boolValue = false;

	bool evaluate(float paramValue, bool paramBool) const {
		switch (op) {
			case Equals:    return paramValue == value;
			case NotEquals: return paramValue != value;
			case Greater:   return paramValue > value;
			case Less:      return paramValue < value;
			case GreaterEq: return paramValue >= value;
			case LessEq:    return paramValue <= value;
			case BoolTrue:  return paramBool;
			case BoolFalse: return !paramBool;
		}
		return false;
	}
};

struct AnimTransition {
	std::string targetState;
	float blendDuration = 0.2f;
	std::vector<AnimCondition> conditions;
	bool hasExitTime = true;
	float exitTime = 0.9f;
};

struct AnimState {
	std::string name;
	std::string clipName;
	float speed = 1.0f;
	bool loop = true;
	std::vector<AnimTransition> transitions;
};

class AnimationController {
public:
	AnimationController() = default;

	void addState(const AnimState& state) {
		m_states[state.name] = state;
		if (m_states.size() == 1) m_currentState = state.name;
	}

	void addTransition(const std::string& from, const AnimTransition& trans) {
		auto it = m_states.find(from);
		if (it != m_states.end()) {
			it->second.transitions.push_back(trans);
		}
	}

	void setParamFloat(const std::string& name, float value) { m_floatParams[name] = value; }
	void setParamBool(const std::string& name, bool value) { m_boolParams[name] = value; }

	float getParamFloat(const std::string& name) const {
		auto it = m_floatParams.find(name);
		return it != m_floatParams.end() ? it->second : 0;
	}

	bool getParamBool(const std::string& name) const {
		auto it = m_boolParams.find(name);
		return it != m_boolParams.end() ? it->second : false;
	}

	void setFloat(const std::string& name, float v) { setParamFloat(name, v); }
	void setBool(const std::string& name, bool v) { setParamBool(name, v); }
	void trigger(const std::string& name) { setParamBool(name, true); }

	void update(float dt) {
		auto it = m_states.find(m_currentState);
		if (it == m_states.end()) return;

		m_stateTime += dt;

		for (auto& trans : it->second.transitions) {
			bool conditionsMet = true;
			for (auto& cond : trans.conditions) {
				float fv = getParamFloat(cond.paramName);
				bool bv = getParamBool(cond.paramName);
				if (!cond.evaluate(fv, bv)) { conditionsMet = false; break; }
			}

			if (trans.hasExitTime && m_stateTime < trans.exitTime * clipDuration(it->second)) {
				conditionsMet = false;
			}

			if (conditionsMet) {
				transitionTo(trans.targetState, trans.blendDuration);
				break;
			}
		}

		if (m_blending && m_blendTimer > 0) {
			m_blendTimer -= dt;
			if (m_blendTimer <= 0) m_blending = false;
		}
	}

	const std::string& currentState() const { return m_currentState; }
	const std::string& currentClip() const {
		auto it = m_states.find(m_currentState);
		return it != m_states.end() ? it->second.clipName : m_currentState;
	}
	float currentSpeed() const {
		auto it = m_states.find(m_currentState);
		return it != m_states.end() ? it->second.speed : 1.0f;
	}
	bool currentLoop() const {
		auto it = m_states.find(m_currentState);
		return it != m_states.end() ? it->second.loop : true;
	}
	float stateTime() const { return m_stateTime; }
	bool isBlending() const { return m_blending; }
	float blendWeight() const { return m_blending ? 1.0f - (m_blendTimer / m_blendDuration) : 0; }
	const std::string& blendFromState() const { return m_blendFromState; }

	void forceState(const std::string& state, float blendTime = 0.1f) {
		transitionTo(state, blendTime);
	}

	const std::unordered_map<std::string, AnimState>& states() const { return m_states; }

private:
	void transitionTo(const std::string& state, float blendDuration) {
		if (m_states.count(state) == 0) return;
		m_blendFromState = m_currentState;
		m_currentState = state;
		m_stateTime = 0;
		m_blending = blendDuration > 0;
		m_blendTimer = blendDuration;
		m_blendDuration = blendDuration;

		for (auto& [_, v] : m_boolParams) v = false;
	}

	float clipDuration(const AnimState&) const { return 1.0f; }

	std::unordered_map<std::string, AnimState> m_states;
	std::unordered_map<std::string, float> m_floatParams;
	std::unordered_map<std::string, bool> m_boolParams;

	std::string m_currentState;
	std::string m_blendFromState;
	float m_stateTime = 0;
	bool m_blending = false;
	float m_blendTimer = 0;
	float m_blendDuration = 0;
};

} // namespace tucano::anim
