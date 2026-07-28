#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace tucano::anim {

struct BlendSample {
	float paramValue = 0.0f;
	const AnimationClip* clip = nullptr;
	float playRate = 1.0f;
};

class BlendSpace1D {
public:
	BlendSpace1D() = default;

	void setParamName(const std::string& name) { m_paramName = name; }
	const std::string& paramName() const { return m_paramName; }

	void addSample(float paramValue, const AnimationClip* clip, float playRate = 1.0f) {
		m_samples.push_back({paramValue, clip, playRate});
		std::sort(m_samples.begin(), m_samples.end(),
		          [](const BlendSample& a, const BlendSample& b) { return a.paramValue < b.paramValue; });
	}

	void setParamRange(float min, float max) { m_min = min; m_max = max; }

	void evaluate(float paramValue, float time, Pose& out, const Skeleton& skeleton) const {
		paramValue = std::clamp(paramValue, m_min, m_max);
		out = skeleton.bindPose();
		if (m_samples.empty()) return;

		if (m_samples.size() == 1) {
			if (m_samples[0].clip) {
				m_samples[0].clip->sample(time * m_samples[0].playRate, WrapMode::Loop, out);
			}
			return;
		}

		int idx = 0;
		for (size_t i = 1; i < m_samples.size(); ++i) {
			if (paramValue < m_samples[i].paramValue) break;
			idx = int(i);
		}

		if (idx == 0) {
			if (m_samples[0].clip) {
				m_samples[0].clip->sample(time * m_samples[0].playRate, WrapMode::Loop, out);
			}
			return;
		}

		if (idx >= int(m_samples.size()) && m_samples.back().clip) {
			m_samples.back().clip->sample(time * m_samples.back().playRate, WrapMode::Loop, out);
			return;
		}

		const auto& a = m_samples[size_t(idx - 1)];
		const auto& b = m_samples[size_t(idx)];
		float range = b.paramValue - a.paramValue;
		float t = range > 0 ? (paramValue - a.paramValue) / range : 0;
		t = std::clamp(t, 0.0f, 1.0f);

		Pose poseA = skeleton.bindPose();
		Pose poseB = skeleton.bindPose();

		if (a.clip) a.clip->sample(time * a.playRate, WrapMode::Loop, poseA);
		if (b.clip) b.clip->sample(time * b.playRate, WrapMode::Loop, poseB);

		AnimationClip::blend(poseA, poseB, t, out);
	}

	float paramMin() const { return m_min; }
	float paramMax() const { return m_max; }
	size_t sampleCount() const { return m_samples.size(); }
	const BlendSample& sample(size_t i) const { return m_samples[i]; }

private:
	std::string m_paramName = "Param";
	std::vector<BlendSample> m_samples;
	float m_min = 0.0f;
	float m_max = 1.0f;
};

struct BlendSample2D {
	float paramX = 0.0f;
	float paramY = 0.0f;
	const AnimationClip* clip = nullptr;
	float playRate = 1.0f;
};

class BlendSpace2D {
public:
	void addSample(float x, float y, const AnimationClip* clip, float playRate = 1.0f) {
		m_samples.push_back({x, y, clip, playRate});
	}

	void evaluate(float x, float y, float time, Pose& out, const Skeleton& skeleton) const {
		out = skeleton.bindPose();
		if (m_samples.empty()) return;

		if (m_samples.size() == 1) {
			if (m_samples[0].clip) {
				m_samples[0].clip->sample(time * m_samples[0].playRate, WrapMode::Loop, out);
			}
			return;
		}

		size_t closest[3] = {0, 1, 1};
		float dists[3] = {
			sqDist(x, y, m_samples[0].paramX, m_samples[0].paramY),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max()
		};

		for (size_t i = 1; i < m_samples.size(); ++i) {
			float d = sqDist(x, y, m_samples[i].paramX, m_samples[i].paramY);
			if (d < dists[0]) {
				dists[2] = dists[1]; closest[2] = closest[1];
				dists[1] = dists[0]; closest[1] = closest[0];
				dists[0] = d;         closest[0] = i;
			} else if (d < dists[1]) {
				dists[2] = dists[1]; closest[2] = closest[1];
				dists[1] = d;         closest[1] = i;
			} else if (d < dists[2]) {
				dists[2] = d;         closest[2] = i;
			}
		}

		Pose poses[3];
		for (int i = 0; i < 3; ++i) {
			poses[i] = skeleton.bindPose();
			if (m_samples[closest[i]].clip) {
				m_samples[closest[i]].clip->sample(time * m_samples[closest[i]].playRate, WrapMode::Loop, poses[i]);
			}
		}

		float total = dists[0] + dists[1] + dists[2];
		if (total < 0.0001f) {
			out = poses[0];
			return;
		}

		float w0 = 1.0f - (dists[0] / total);
		float w1 = 1.0f - (dists[1] / total);
		float w2 = 1.0f - (dists[2] / total);
		float wSum = w0 + w1 + w2;

		Pose blended = skeleton.bindPose();
		for (size_t i = 0; i < out.size(); ++i) {
			float weight = w0 / wSum;
			blended.positions[i] = poses[0].positions[i] * weight;
			blended.rotations[i] = glm::slerp(glm::quat(1,0,0,0), poses[0].rotations[i], weight);
			blended.scales[i] = poses[0].scales[i] * weight;

			weight = w1 / wSum;
			blended.positions[i] += poses[1].positions[i] * weight;
			glm::quat q1 = poses[1].rotations[i];
			if (glm::dot(blended.rotations[i], q1) < 0) q1 = -q1;
			blended.rotations[i] = glm::slerp(blended.rotations[i], glm::normalize(blended.rotations[i] + q1 * weight), 0.5f);
			blended.scales[i] += poses[1].scales[i] * weight;

			weight = w2 / wSum;
			blended.positions[i] += poses[2].positions[i] * weight;
			glm::quat q2 = poses[2].rotations[i];
			if (glm::dot(blended.rotations[i], q2) < 0) q2 = -q2;
			blended.rotations[i] = glm::slerp(blended.rotations[i], glm::normalize(blended.rotations[i] + q2 * weight), 0.5f);
			blended.scales[i] += poses[2].scales[i] * weight;
		}
		out = blended;
	}

	size_t sampleCount() const { return m_samples.size(); }

private:
	static float sqDist(float x, float y, float sx, float sy) {
		float dx = x - sx, dy = y - sy;
		return dx * dx + dy * dy;
	}

	std::vector<BlendSample2D> m_samples;
};

} // namespace tucano::anim
