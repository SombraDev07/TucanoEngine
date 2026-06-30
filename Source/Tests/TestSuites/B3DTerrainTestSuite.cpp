//************************************* B3D Framework - Copyright 2026 TucanoEngine / B3DFramework *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DTerrainTestSuite.h"
#include "Terrain/B3DHeightmapData.h"
#include "Terrain/B3DTerrainLodGrid.h"
#include "Terrain/B3DTerrainLandClass.h"
#include "Math/B3DMath.h"

using namespace b3d;

TerrainTestSuite::TerrainTestSuite()
	: TestSuite("TerrainTestSuite")
{
	B3D_ADD_TEST(TerrainTestSuite::TestHeightmapMinMaxAndQueries)
	B3D_ADD_TEST(TerrainTestSuite::TestLodGridCulling)
	B3D_ADD_TEST(TerrainTestSuite::TestLandClassPainting)
}

void TerrainTestSuite::TestHeightmapMinMaxAndQueries()
{
	// Create a flat 64x64 heightmap with a small mound in the center
	constexpr uint32_t width = 64;
	constexpr uint32_t height = 64;
	TArray<uint16_t> rawData(width * height, 0);

	// Put a pyramid peak at center
	// height range: raw = [0, 65535], world = [0.0f, 100.0f]
	for (uint32_t z = 0; z < height; ++z)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			int32_t dx = (int32_t)x - 32;
			int32_t dz = (int32_t)z - 32;
			int32_t dist = Math::Max(abs(dx), abs(dz));
			if (dist < 10)
			{
				rawData[z * width + x] = (uint16_t)((10 - dist) * 6553);
			}
		}
	}

	HeightmapData heightmap;
	bool ok = heightmap.Init(rawData.data(), width, height,
		2.0f,       // cellSize
		10.0f,      // heightMin
		200.0f,     // heightScale
		Vector2(-64.0f, -64.0f)); // worldOffset

	B3D_TEST_ASSERT(ok);
	B3D_TEST_ASSERT(heightmap.IsValid());

	// Test boundary checks
	B3D_TEST_ASSERT(heightmap.GetWidthSamples() == width);
	B3D_TEST_ASSERT(heightmap.GetHeightSamples() == height);

	// World bounds check
	AABox wb = heightmap.GetWorldBounds();
	B3D_TEST_ASSERT(Math::ApproxEquals(wb.Minimum.X, -64.0f));
	B3D_TEST_ASSERT(Math::ApproxEquals(wb.Minimum.Z, -64.0f));

	// Query flat corners
	float flatH = heightmap.GetHeightAtWorld(-64.0f, -64.0f);
	B3D_TEST_ASSERT(Math::ApproxEquals(flatH, 10.0f));

	// Query peak (at grid x=32, z=32 -> world x = -64 + 32*2 = 0, z = -64 + 32*2 = 0)
	float peakH = heightmap.GetHeightAtWorld(0.0f, 0.0f);
	B3D_TEST_ASSERT(peakH > 10.0f);

	// Raycast check: downward ray hitting the peak
	float dist = heightmap.TraceRay(Vector3(0.0f, 500.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 1000.0f);
	B3D_TEST_ASSERT(dist > 0.0f);
	float hitY = 500.0f - dist;
	B3D_TEST_ASSERT(Math::ApproxEquals(hitY, peakH, 0.01f));
}

void TerrainTestSuite::TestLodGridCulling()
{
	constexpr uint32_t width = 64;
	constexpr uint32_t height = 64;
	TArray<uint16_t> rawData(width * height, 0);

	HeightmapData heightmap;
	heightmap.Init(rawData.data(), width, height, 1.0f, 0.0f, 10.0f, Vector2::kZero);

	TerrainLodGrid lodGrid;
	TerrainLodGrid::Settings settings;
	settings.mPatchDim = 8;
	settings.mLodCount = 3;
	settings.mLod0PatchSize = 8.0f;
	settings.mLod0Radius = 16.0f;

	bool ok = lodGrid.Init(settings, &heightmap);
	B3D_TEST_ASSERT(ok);

	// Cull from camera at center
	TerrainLodCullResult cullResult;
	// Use projection matrix to build ConvexVolume
	Matrix4 proj = Matrix4::kIdentity;
	ConvexVolume frustum(proj);

	lodGrid.Cull(frustum, Vector3(32.0f, 5.0f, 32.0f), cullResult);

	B3D_TEST_ASSERT(cullResult.mPatches.size() > 0);
	B3D_TEST_ASSERT(cullResult.mLod0PatchCount > 0);
}

void TerrainTestSuite::TestLandClassPainting()
{
	TerrainLandClassManager manager;
	bool ok = manager.Init(128); // 128x128 splat mask
	B3D_TEST_ASSERT(ok);

	// Add simple land class
	TerrainLandClass lc;
	lc.mName = "TestGrass";
	lc.mSlopeMin = 0.0f;
	lc.mSlopeMax = 1.0f;
	lc.mAltitudeMin = -100.0f;
	lc.mAltitudeMax = 1000.0f;
	uint32_t idx = manager.AddLandClass(lc);
	B3D_TEST_ASSERT(idx == 0);

	// Paint center of the terrain (terrain origin [0,0], size 100m)
	manager.Paint(0, Vector2(50.0f, 50.0f), 10.0f, 1.0f, Vector2::kZero, 100.0f);

	B3D_TEST_ASSERT(manager.GetLandClassCount() == 1);
	B3D_TEST_ASSERT(manager.GetLandClass(0)->mName == "TestGrass");
}
