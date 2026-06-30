//************************************* B3D Framework - Copyright 2026 TucanoEngine / B3DFramework *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "Testing/B3DTestSuite.h"

namespace b3d
{
	class TerrainTestSuite : public TestSuite
	{
	public:
		TerrainTestSuite();

	private:
		void TestHeightmapMinMaxAndQueries();
		void TestLodGridCulling();
		void TestLandClassPainting();
	};
} // namespace b3d
