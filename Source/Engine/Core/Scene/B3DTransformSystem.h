//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "ECS/B3DRegistry.h"

namespace b3d::ecs
{
	/** @addtogroup Scene-Internal
	 *  @{
	 */

	/** Processes scene object transform hierarchy: propagates dirty flags down the hierarchy and recomputes world-space transforms. */
	class B3D_EXPORT TransformSystem
	{
	public:
		/** Runs the full transform update: propagate dirty flags, then recompute world transforms. */
		static void Update(Registry& registry);

	private:
		/** Iterates through children of dirty entities, ensuring every descendant of a dirty entity is also tagged TransformDirty. */
		 // NOTE: Currently redundant — NotifyTransformChanged() already recurses to children and tags each one. This method exists as a safety net and becomes essential when/if NotifyTransformChanged's child recursion is removed in a future phase.
		static void PropagateDirtyFlags(Registry& registry);

		/** Sorts dirty entities by hierarchy depth and recomputes WorldTransform for each. */
		static void ComputeWorldTransforms(Registry& registry);
	};

	/** @} */
} // namespace b3d::ecs
