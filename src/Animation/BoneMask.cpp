#include "Animation/BoneMask.h"
#include "Animation/Skeleton.h"

#include <algorithm>

namespace tucano::animation {

void BoneMask::apply(tucano::anim::Pose& base, const tucano::anim::Pose& overlay, const BoneMask& mask) {
	size_t n = std::min({base.size(), overlay.size(), mask.boneCount()});
	for (size_t i = 0; i < n; ++i) {
		float w = mask.weight(i);
		if (w <= 0.0f) continue;
		// Simple lerp for now
		base.positions[i] = glm::mix(base.positions[i], overlay.positions[i], w);
		base.scales[i] = glm::mix(base.scales[i], overlay.scales[i], w);
		// Slerp for rotation
		glm::quat qb = overlay.rotations[i];
		if (glm::dot(base.rotations[i], qb) < 0.0f) qb = -qb;
		base.rotations[i] = glm::normalize(glm::slerp(base.rotations[i], qb, w));
	}
}

} // namespace tucano::animation
