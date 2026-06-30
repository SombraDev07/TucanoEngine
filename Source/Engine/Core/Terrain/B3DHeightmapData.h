//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DAABox.h"

namespace b3d
{
	/**
	 * Stores terrain heightmap data and provides height/normal queries.
	 *
	 * Internally keeps a hierarchical min/max structure (inspired by Dagor Engine's
	 * CompressedHeightmap) for O(log n) raycasting and efficient LOD culling.
	 * Height samples are stored as uint16 to minimize memory footprint.
	 */
	class B3D_EXPORT HeightmapData
	{
	public:
		/** Block dimension used for the min/max hierarchy (32×32 samples per block). */
		static constexpr uint32_t kBlockSize   = 32;
		static constexpr uint32_t kBlockSizeSq = kBlockSize * kBlockSize;
		static constexpr uint32_t kMaxHierarchyLevels = 8;

		/**
		 * Min/max height range for one block in the hierarchy.
		 * Heights are stored as uint16 and converted to world space on demand.
		 */
		struct BlockRange
		{
			uint16_t mHeightMin = 0;
			uint16_t mHeightMax = 0;
		};

		HeightmapData() = default;
		~HeightmapData() { Close(); }

		HeightmapData(const HeightmapData&) = delete;
		HeightmapData& operator=(const HeightmapData&) = delete;

		/**
		 * Initialises the heightmap from a raw uint16 array.
		 * Builds the min/max hierarchy automatically.
		 *
		 * @param rawData       Array of widthSamples * heightSamples uint16 values.
		 * @param widthSamples  Number of samples along X (should be power-of-2 + 1).
		 * @param heightSamples Number of samples along Z (should be power-of-2 + 1).
		 * @param cellSize      World-space distance between adjacent samples (meters).
		 * @param heightMin     World-space height corresponding to raw value 0.
		 * @param heightScale   World-space height range (max - min).
		 * @param worldOffset   World-space XZ origin of the heightmap corner.
		 * @return True on success.
		 */
		bool Init(const uint16_t* rawData, uint32_t widthSamples, uint32_t heightSamples,
			float cellSize, float heightMin, float heightScale, Vector2 worldOffset);

		/** Releases all memory. */
		void Close();

		// ---- Height / Normal Queries ----

		/** Returns the bilinear-interpolated world height at a world-space XZ position. */
		float GetHeightAtWorld(float worldX, float worldZ) const;

		/**
		 * Returns height and surface normal at a world-space XZ position.
		 * @return False if the position is outside the heightmap bounds.
		 */
		bool GetHeightAndNormal(Vector2 worldXZ, float& outHeight, Vector3& outNormal) const;

		/**
		 * Returns the min and max world height within a world-space rectangular chunk.
		 * Used by the LOD culling system to reject invisible patches.
		 */
		void GetMinMaxInChunk(Vector2 chunkOriginXZ, float chunkSizeMeters,
			float& outMin, float& outMax) const;

		/**
		 * Traces a ray against the heightmap and returns the hit distance, or -1 if no hit.
		 * Uses the hierarchical min/max to skip empty regions quickly.
		 */
		float TraceRay(Vector3 rayOrigin, Vector3 rayDir, float maxDist) const;

		// ---- Raw sample access ----

		/** Returns the raw uint16 sample at integer texel coordinates (x, z). */
		uint16_t GetSampleRaw(uint32_t x, uint32_t z) const
		{
			B3D_ASSERT(x < mWidthSamples && z < mHeightSamples);
			return mHeightData[z * mWidthSamples + x];
		}

		/** Converts a raw uint16 sample to a world-space height value. */
		float SampleToHeight(uint16_t raw) const { return mHeightMin + raw * mHeightScaleRaw; }

		// ---- Accessors ----

		uint32_t GetWidthSamples()  const { return mWidthSamples; }
		uint32_t GetHeightSamples() const { return mHeightSamples; }
		float    GetCellSize()      const { return mCellSizeMeters; }
		float    GetHeightMin()     const { return mHeightMin; }
		float    GetHeightScale()   const { return mHeightScale; }
		Vector2  GetWorldOffset()   const { return mWorldOffset; }
		AABox    GetWorldBounds()   const { return mWorldBounds; }
		bool     IsValid()          const { return !mHeightData.Empty(); }

	private:
		/** One level of the min/max hierarchy. */
		struct HierarchyLevel
		{
			TArray<BlockRange> mBlocks;
			uint32_t           mStride = 0;     // blocks per row at this level
			float              mChunkSize = 0;  // world size of one block
		};

		/** Builds all hierarchy levels from the base sample data. */
		void BuildMinMaxHierarchy();

		/** Returns the min/max range of a block at a given hierarchy level. */
		void GetBlockRange(uint32_t level, uint32_t blockX, uint32_t blockZ,
			uint16_t& outMin, uint16_t& outMax) const;

		// ---- Height query helpers ----

		/** Converts world-space XZ to integer texel grid coordinates. */
		void WorldToTexel(float worldX, float worldZ, float& texelX, float& texelZ) const;

		/** Samples the height grid with bilinear interpolation. */
		float SampleBilinear(float texelX, float texelZ) const;

		// ---- Data ----

		uint32_t         mWidthSamples  = 0;
		uint32_t         mHeightSamples = 0;
		float            mCellSizeMeters = 1.0f;
		float            mHeightMin      = 0.0f;
		float            mHeightScale    = 1.0f;
		float            mHeightScaleRaw = 0.0f; // mHeightScale / 65535.0 — avoids division per sample
		float            mInvCellSize    = 1.0f;
		Vector2          mWorldOffset    = Vector2::kZero;
		AABox            mWorldBounds;

		TArray<uint16_t>     mHeightData;                          // raw samples, width * height
		HierarchyLevel       mHierarchy[kMaxHierarchyLevels];
		uint32_t             mHierarchyLevels = 0;
	};
}
