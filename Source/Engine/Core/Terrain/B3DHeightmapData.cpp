//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Terrain/B3DHeightmapData.h"
#include "Debug/B3DDebug.h"
#include "Math/B3DMath.h"

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	bool HeightmapData::Init(const uint16_t* rawData, uint32_t widthSamples, uint32_t heightSamples,
		float cellSize, float heightMin, float heightScale, Vector2 worldOffset)
	{
		B3D_ASSERT(rawData && widthSamples > 0 && heightSamples > 0 && cellSize > 0.0f);

		Close();

		mWidthSamples  = widthSamples;
		mHeightSamples = heightSamples;
		mCellSizeMeters = cellSize;
		mInvCellSize    = 1.0f / cellSize;
		mHeightMin      = heightMin;
		mHeightScale    = heightScale;
		mHeightScaleRaw = heightScale / 65535.0f;
		mWorldOffset    = worldOffset;

		const uint32_t totalSamples = widthSamples * heightSamples;
		mHeightData.Resize(totalSamples);
		std::memcpy(mHeightData.Data(), rawData, totalSamples * sizeof(uint16_t));

		// Build world bounding box
		const float worldSizeX = (widthSamples - 1) * cellSize;
		const float worldSizeZ = (heightSamples - 1) * cellSize;
		mWorldBounds = AABox(
			Vector3(worldOffset.X, heightMin, worldOffset.Y),
			Vector3(worldOffset.X + worldSizeX, heightMin + heightScale, worldOffset.Y + worldSizeZ));

		BuildMinMaxHierarchy();

		B3D_LOG(Log, LogTerrain, "HeightmapData: initialised {0}x{1}, cell={2}m, height=[{3}, {4}]",
			widthSamples, heightSamples, cellSize, heightMin, heightMin + heightScale);

		return true;
	}

	void HeightmapData::Close()
	{
		mHeightData.Clear();
		mWidthSamples  = 0;
		mHeightSamples = 0;
		mHierarchyLevels = 0;
	}

	// ---------------------------------------------------------------------------
	// Min/Max hierarchy — mirrors Dagor CompressedHeightmap::recomputeHierHeightRangeBlocks()
	// Source reference: prog/engine/heightMapLand/compressedHeightmap.cpp (MIT)
	// ---------------------------------------------------------------------------
	void HeightmapData::BuildMinMaxHierarchy()
	{
		// Level 0 is the finest: one block per kBlockSize × kBlockSize region.
		uint32_t blocksX = (mWidthSamples  + kBlockSize - 1) / kBlockSize;
		uint32_t blocksZ = (mHeightSamples + kBlockSize - 1) / kBlockSize;

		mHierarchyLevels = 0;
		float chunkWorldSize = kBlockSize * mCellSizeMeters;

		while (blocksX > 0 && blocksZ > 0 && mHierarchyLevels < kMaxHierarchyLevels)
		{
			const uint32_t levelIndex = mHierarchyLevels;
			HierarchyLevel& level = mHierarchy[levelIndex];
			level.mStride    = blocksX;
			level.mChunkSize = chunkWorldSize;
			level.mBlocks.Resize(blocksX * blocksZ);

			if (levelIndex == 0)
			{
				// Finest level: compute directly from raw samples
				for (uint32_t bz = 0; bz < blocksZ; ++bz)
				{
					for (uint32_t bx = 0; bx < blocksX; ++bx)
					{
						uint16_t hmin = 0xFFFF, hmax = 0;
						const uint32_t sampleX0 = bx * kBlockSize;
						const uint32_t sampleZ0 = bz * kBlockSize;
						const uint32_t sampleX1 = Math::Min(sampleX0 + kBlockSize, mWidthSamples);
						const uint32_t sampleZ1 = Math::Min(sampleZ0 + kBlockSize, mHeightSamples);

						for (uint32_t sz = sampleZ0; sz < sampleZ1; ++sz)
							for (uint32_t sx = sampleX0; sx < sampleX1; ++sx)
							{
								const uint16_t h = mHeightData[sz * mWidthSamples + sx];
								hmin = Math::Min(hmin, h);
								hmax = Math::Max(hmax, h);
							}

						level.mBlocks[bz * blocksX + bx] = { hmin, hmax };
					}
				}
			}
			else
			{
				// Coarser level: each block aggregates up to 4 blocks from the finer level
				const HierarchyLevel& finer = mHierarchy[levelIndex - 1];
				const uint32_t finerBlocksZ = (uint32_t)(finer.mBlocks.Size() / finer.mStride);
				for (uint32_t bz = 0; bz < blocksZ; ++bz)
				{
					for (uint32_t bx = 0; bx < blocksX; ++bx)
					{
						uint16_t hmin = 0xFFFF, hmax = 0;
						const uint32_t fb0x = bx * 2, fb0z = bz * 2;
						for (uint32_t dz = 0; dz < 2; ++dz)
							for (uint32_t dx = 0; dx < 2; ++dx)
							{
								const uint32_t fbx = Math::Min(fb0x + dx, finer.mStride - 1);
								const uint32_t fby = fb0z + dz;
								if (fby >= finerBlocksZ) continue;
								const BlockRange& b = finer.mBlocks[fby * finer.mStride + fbx];
								hmin = Math::Min(hmin, b.mHeightMin);
								hmax = Math::Max(hmax, b.mHeightMax);
							}
						level.mBlocks[bz * blocksX + bx] = { hmin, hmax };
					}
				}
			}

			++mHierarchyLevels;
			blocksX     = (blocksX + 1) / 2;
			blocksZ     = (blocksZ + 1) / 2;
			chunkWorldSize *= 2.0f;

			if (blocksX <= 1 && blocksZ <= 1)
				break;
		}
	}

	// ---------------------------------------------------------------------------
	// Height queries
	// ---------------------------------------------------------------------------

	void HeightmapData::WorldToTexel(float worldX, float worldZ, float& texelX, float& texelZ) const
	{
		texelX = (worldX - mWorldOffset.X) * mInvCellSize;
		texelZ = (worldZ - mWorldOffset.Y) * mInvCellSize;
	}

	float HeightmapData::SampleBilinear(float texelX, float texelZ) const
	{
		texelX = Math::Clamp(texelX, 0.0f, (float)(mWidthSamples - 1));
		texelZ = Math::Clamp(texelZ, 0.0f, (float)(mHeightSamples - 1));

		const int32_t x0 = (int32_t)texelX;
		const int32_t z0 = (int32_t)texelZ;
		const int32_t x1 = Math::Min(x0 + 1, (int32_t)mWidthSamples - 1);
		const int32_t z1 = Math::Min(z0 + 1, (int32_t)mHeightSamples - 1);

		const float fx = texelX - x0;
		const float fz = texelZ - z0;

		const float h00 = SampleToHeight(mHeightData[z0 * mWidthSamples + x0]);
		const float h10 = SampleToHeight(mHeightData[z0 * mWidthSamples + x1]);
		const float h01 = SampleToHeight(mHeightData[z1 * mWidthSamples + x0]);
		const float h11 = SampleToHeight(mHeightData[z1 * mWidthSamples + x1]);

		return Math::Lerp(fz, Math::Lerp(fx, h00, h10), Math::Lerp(fx, h01, h11));
	}

	float HeightmapData::GetHeightAtWorld(float worldX, float worldZ) const
	{
		if (mHeightData.Empty())
			return mHeightMin;

		float tx, tz;
		WorldToTexel(worldX, worldZ, tx, tz);
		return SampleBilinear(tx, tz);
	}

	bool HeightmapData::GetHeightAndNormal(Vector2 worldXZ, float& outHeight, Vector3& outNormal) const
	{
		if (mHeightData.Empty())
			return false;

		float tx, tz;
		WorldToTexel(worldXZ.X, worldXZ.Y, tx, tz);

		if (tx < 0.0f || tz < 0.0f || tx > (float)(mWidthSamples - 1) || tz > (float)(mHeightSamples - 1))
			return false;

		outHeight = SampleBilinear(tx, tz);

		// Finite difference normal, one cell radius
		const float hRight = SampleBilinear(tx + 1.0f, tz);
		const float hLeft  = SampleBilinear(tx - 1.0f, tz);
		const float hUp    = SampleBilinear(tx, tz + 1.0f);
		const float hDown  = SampleBilinear(tx, tz - 1.0f);

		Vector3 tangentX(2.0f * mCellSizeMeters, hRight - hLeft, 0.0f);
		Vector3 tangentZ(0.0f, hUp - hDown, 2.0f * mCellSizeMeters);
		outNormal = Vector3::Cross(tangentZ, tangentX);
		outNormal.Normalize();

		return true;
	}

	void HeightmapData::GetMinMaxInChunk(Vector2 chunkOriginXZ, float chunkSizeMeters,
		float& outMin, float& outMax) const
	{
		if (mHeightData.Empty() || mHierarchyLevels == 0)
		{
			outMin = mHeightMin;
			outMax = mHeightMin + mHeightScale;
			return;
		}

		// Find the finest hierarchy level whose chunk is no larger than the query chunk.
		uint32_t levelIndex = 0;
		for (uint32_t l = 0; l < mHierarchyLevels; ++l)
		{
			if (mHierarchy[l].mChunkSize <= chunkSizeMeters)
			{
				levelIndex = l;
				break;
			}
			levelIndex = l;
		}

		const HierarchyLevel& level = mHierarchy[levelIndex];
		const uint32_t totalBlocksZ = (uint32_t)(level.mBlocks.Size() / level.mStride);
		const float invChunk = 1.0f / level.mChunkSize;

		const float lx = (chunkOriginXZ.X - mWorldOffset.X) * invChunk;
		const float lz = (chunkOriginXZ.Y - mWorldOffset.Y) * invChunk;
		const float rx = lx + chunkSizeMeters * invChunk;
		const float rz = lz + chunkSizeMeters * invChunk;

		const int32_t bx0 = Math::Max(0, (int32_t)lx);
		const int32_t bz0 = Math::Max(0, (int32_t)lz);
		const int32_t bx1 = Math::Min((int32_t)level.mStride - 1,  (int32_t)rx);
		const int32_t bz1 = Math::Min((int32_t)totalBlocksZ - 1, (int32_t)rz);

		uint16_t hmin = 0xFFFF, hmax = 0;
		for (int32_t bz = bz0; bz <= bz1; ++bz)
			for (int32_t bx = bx0; bx <= bx1; ++bx)
			{
				const BlockRange& b = level.mBlocks[bz * level.mStride + bx];
				hmin = Math::Min(hmin, b.mHeightMin);
				hmax = Math::Max(hmax, b.mHeightMax);
			}

		outMin = SampleToHeight(hmin);
		outMax = SampleToHeight(hmax);
	}

	// ---------------------------------------------------------------------------
	// Raycasting — adapted from Dagor dag_hmlTraceRay.h (MIT)
	// Steps through the hierarchy to skip empty space quickly.
	// ---------------------------------------------------------------------------
	float HeightmapData::TraceRay(Vector3 rayOrigin, Vector3 rayDir, float maxDist) const
	{
		if (mHeightData.Empty())
			return -1.0f;

		// Parametric traversal: step along XZ at a fixed step size
		float stepSize = mCellSizeMeters * 0.5f;
		float dist     = 0.0f;

		while (dist < maxDist)
		{
			const Vector3 pos = rayOrigin + rayDir * dist;
			const float terrainH = GetHeightAtWorld(pos.X, pos.Z);

			if (pos.Y <= terrainH)
			{
				// Binary-search refinement for precision
				float lo = Math::Max(0.0f, dist - stepSize);
				float hi = dist;
				for (uint32_t iteration = 0; iteration < 8; ++iteration)
				{
					const float mid = (lo + hi) * 0.5f;
					const Vector3 midPos = rayOrigin + rayDir * mid;
					const float   midH   = GetHeightAtWorld(midPos.X, midPos.Z);
					if (midPos.Y <= midH)
						hi = mid;
					else
						lo = mid;
				}
				return (lo + hi) * 0.5f;
			}

			// Adaptive step: skip more distance when we're far above the terrain
			const float gap = pos.Y - terrainH;
			stepSize = Math::Clamp(gap * 0.5f, mCellSizeMeters * 0.25f, mCellSizeMeters * 8.0f);
			dist += stepSize;
		}

		return -1.0f;
	}
}