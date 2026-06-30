//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "Testing/B3DTestSuite.h"

namespace b3d
{
	class SceneObjectTransformTestSuite : public TestSuite
	{
	public:
		SceneObjectTransformTestSuite();

	private:
		// Phase 1: ECS Fragment Tests
		void TestLocalTransformFragment();
		void TestWorldTransformFragment();
		void TestParentChildFragments();
		void TestHierarchyDepthFragment();
		void TestTransformDirtyTag();
		void TestMobilityTags();
		void TestMobilityAffectsTransformDirty();

		// Phase 2: TransformSystem Tests
		void TestTransformSystemRootOnly();
		void TestTransformSystemHierarchy();
		void TestTransformSystemDirtyPropagation();
		void TestTransformSystemPartialDirty();
		void TestTransformSystemMultipleUpdates();

		// Phase 3: Serialization & Cloning Tests
		void TestSceneObjectSerialization();
		void TestSceneObjectClone();
		void TestSceneObjectCloneTransformIndependence();
	};
} // namespace b3d
