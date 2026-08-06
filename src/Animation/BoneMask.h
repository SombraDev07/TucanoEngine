#pragma once

#include <vector>
#include <string>
#include <unordered_map>

// Bone mask for selective animation blending. Works with tucano::anim::Pose.

namespace tucano::anim {
struct Pose;
}

namespace tucano::animation {

class BoneMask {
public:
	BoneMask() = default;

	void resize(size_t boneCount, float defaultValue = 1.0f) {
		m_weights.resize(boneCount, defaultValue);
		m_default = defaultValue;
	}

	float weight(size_t boneIndex) const {
		return boneIndex < m_weights.size() ? m_weights[boneIndex] : m_default;
	}

	void setWeight(size_t boneIndex, float w) {
		if (boneIndex < m_weights.size()) m_weights[boneIndex] = w;
	}

	size_t boneCount() const { return m_weights.size(); }

	// Apply mask: blend overlay into base where mask > 0.
	static void apply(tucano::anim::Pose& base, const tucano::anim::Pose& overlay, const BoneMask& mask);

private:
	std::vector<float> m_weights;
	float m_default = 1.0f;
};

} // namespace tucano::animation
