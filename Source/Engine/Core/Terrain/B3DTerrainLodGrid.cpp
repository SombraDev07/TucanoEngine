//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Terrain/B3DTerrainLodGrid.h"
#include "Terrain/B3DHeightmapData.h"
#include "Math/B3DMath.h"
#include "Utility/B3DBitwise.h"
#include "Debug/B3DDebug.h"

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	bool TerrainLodGrid::Init(const Settings& settings, HeightmapData* heightmap)
	{
		B3D_ASSERT(settings.mLodCount >= 1 && settings.mLodCount <= kMaxLods);
		B3D_ASSERT(settings.mPatchDim >= 2 && Bitwise::IsPow2(settings.mPatchDim));
		B3D_ASSERT(heightmap);

		Close();

		mSettings  = settings;
		mHeightmap = heightmap;

		BuildHeightCache();

		B3D_LOG(Log, LogTerrain,
			"TerrainLodGrid: {0} LODs, patch={1}x{1}, lod0size={2}m",
			settings.mLodCount, settings.mPatchDim, settings.mLod0PatchSize);

		return true;
	}

	void TerrainLodGrid::Close()
	{
		mHeightmap = nullptr;
		mHeightCache.Clear();
	}

	// ---------------------------------------------------------------------------
	// Height cache — stores per-patch min/max heights for AABB frustum culling.
	// One cache entry per LOD level, each patch maps to a grid cell at that scale.
	// Approach: sample heightmap hierarchy at the appropriate chunk size.
	// ---------------------------------------------------------------------------
	void TerrainLodGrid::BuildHeightCache()
	{
		mHeightCache.Resize(mSettings.mLodCount);

		const AABox worldBounds = mHeightmap->GetWorldBounds();
		const float worldSizeX = worldBounds.GetSize().X;
		const float worldSizeZ = worldBounds.GetSize().Z;

		for (uint32_t lodLevel = 0; lodLevel < mSettings.mLodCount; ++lodLevel)
		{
			const float patchSize = mSettings.mLod0PatchSize * (float)(1u << lodLevel);
			PatchHeightCache& cache = mHeightCache[lodLevel];
			cache.mPatchWorldSize = patchSize;

			const uint32_t patchesX = (uint32_t)Math::CeilToPosInt(worldSizeX / patchSize) + 2;
			const uint32_t patchesZ = (uint32_t)Math::CeilToPosInt(worldSizeZ / patchSize) + 2;
			cache.mStride = patchesX;
			cache.mMinMax.Resize(patchesX * patchesZ);

			const Vector2 originXZ(worldBounds.Minimum.X, worldBounds.Minimum.Z);
			for (uint32_t pz = 0; pz < patchesZ; ++pz)
			{
				for (uint32_t px = 0; px < patchesX; ++px)
				{
					const Vector2 chunkOrigin = originXZ + Vector2(px * patchSize, pz * patchSize);
					float hMin, hMax;
					mHeightmap->GetMinMaxInChunk(chunkOrigin, patchSize, hMin, hMax);
					cache.mMinMax[pz * patchesX + px] = Vector2(hMin, hMax);
				}
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Culling — ring-based geo-clipmap traversal.
	// Inspired by Dagor Engine cull_lod_grid() (prog/gameLibs/heightmap/heightmapCulling.cpp, MIT).
	// Key differences:
	//   - Uses B3D ConvexVolume directly instead of Dagor-specific Frustum.
	//   - Height cache uses HeightmapData hierarchy (not Dagor's CompressedHeightmap).
	//   - No hardware tessellation path (can be added on top).
	// ---------------------------------------------------------------------------
	void TerrainLodGrid::Cull(const ConvexVolume& frustum, Vector3 viewPos, TerrainLodCullResult& outResult) const
	{
		outResult.Clear();

		if (!mHeightmap || !mHeightmap->IsValid())
			return;

		// Snap grid origin to finest LOD patch grid (avoids swimming as camera moves)
		const float lod0Size  = mSettings.mLod0PatchSize;
		const float gridSnapX = Math::Floor(viewPos.X / lod0Size) * lod0Size;
		const float gridSnapZ = Math::Floor(viewPos.Z / lod0Size) * lod0Size;

		outResult.mGridOrigin      = Vector2(gridSnapX, gridSnapZ);
		outResult.mWorldToLod0Scale = 1.0f / lod0Size;

		for (uint32_t lodLevel = 0; lodLevel < mSettings.mLodCount; ++lodLevel)
		{
			CullLodRing(lodLevel, outResult.mGridOrigin, frustum, outResult);

			if (lodLevel == 0)
				outResult.mLod0PatchCount = (uint32_t)outResult.mPatches.Size();
		}

		// Mark the start of patches that require vertex shader morphing
		outResult.mMorphStartIndex = outResult.mLod0PatchCount;
	}

	void TerrainLodGrid::CullLodRing(uint32_t lodLevel, Vector2 gridOriginXZ,
		const ConvexVolume& frustum, TerrainLodCullResult& outResult) const
	{
		const float patchSize   = mSettings.mLod0PatchSize * (float)(1u << lodLevel);
		const int32_t ringRadius = (int32_t)mSettings.mLod0Radius * (1 << lodLevel);
		const int32_t innerRadius = ringRadius / 2; // covered by finer LOD

		// Iterate over the ring of patch-grid cells
		for (int32_t pz = -ringRadius; pz < ringRadius; ++pz)
		{
			for (int32_t px = -ringRadius; px < ringRadius; ++px)
			{
				// Skip the inner region already rendered by finer LODs
				if (lodLevel > 0 && px > -innerRadius && px < innerRadius && pz > -innerRadius && pz < innerRadius)
					continue;

				const float worldX = gridOriginXZ.X + px * patchSize;
				const float worldZ = gridOriginXZ.Y + pz * patchSize;

				// Retrieve cached height bounds for this patch
				float hMin = mHeightmap->GetWorldBounds().Minimum.Y;
				float hMax = mHeightmap->GetWorldBounds().Maximum.Y;

				if (!mHeightCache.Empty() && lodLevel < (uint32_t)mHeightCache.Size())
				{
					const PatchHeightCache& cache = mHeightCache[lodLevel];
					const AABox wb = mHeightmap->GetWorldBounds();
					const int32_t cacheX = (int32_t)((worldX - wb.Minimum.X) / patchSize);
					const int32_t cacheZ = (int32_t)((worldZ - wb.Minimum.Z) / patchSize);
					const int32_t cacheW = (int32_t)cache.mStride;
					const int32_t cacheH = cacheW > 0 ? (int32_t)(cache.mMinMax.Size() / cacheW) : 0;
					if (cacheX >= 0 && cacheZ >= 0 && cacheX < cacheW && cacheZ < cacheH)
					{
						const Vector2 mm = cache.mMinMax[cacheZ * cacheW + cacheX];
						hMin = mm.X;
						hMax = mm.Y;
					}
				}

				if (!IsPatchVisible(frustum, worldX, worldZ, patchSize, hMin, hMax))
					continue;

				TerrainPatch patch;
				patch.mOriginX    = worldX;
				patch.mOriginZ    = worldZ;
				patch.mPatchSize  = patchSize;
				patch.mMorphFlags = ComputeMorphFlags(lodLevel, px, pz);

				outResult.mPatches.Add(patch);
			}
		}
	}

	bool TerrainLodGrid::IsPatchVisible(const ConvexVolume& frustum, float patchX, float patchZ,
		float patchSize, float hMin, float hMax) const
	{
		const AABox patchBounds(
			Vector3(patchX,            hMin, patchZ),
			Vector3(patchX + patchSize, hMax, patchZ + patchSize));

		return frustum.Intersects(patchBounds);
	}

	uint32_t TerrainLodGrid::ComputeMorphFlags(uint32_t lodLevel, int32_t patchGridX, int32_t patchGridZ) const
	{
		if (lodLevel == 0)
			return 0; // finest LOD never morphs

		// Bit 0: enable vertex morph toward coarser LOD
		// Bits 1-4: edge connectivity (which edges border a coarser LOD)
		uint32_t flags = 1u; // morph enabled

		// If the patch is on the inner boundary of this ring, its edges face a
		// finer LOD and need transition (crack prevention). We encode this in bits 1-4.
		const int32_t innerRadius = (int32_t)mSettings.mLod0Radius * (1 << (lodLevel - 1));
		if (patchGridX == -innerRadius || patchGridX == innerRadius - 1) flags |= (1u << 1);
		if (patchGridZ == -innerRadius || patchGridZ == innerRadius - 1) flags |= (1u << 2);

		return flags;
	}
}