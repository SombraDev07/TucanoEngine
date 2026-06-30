//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DVector4.h"
#include "Math/B3DAABox.h"
#include "Math/B3DConvexVolume.h"

namespace b3d
{
	class HeightmapData;

	/**
	 * Describes a single renderable terrain patch after LOD culling.
	 *
	 * Matches the layout of LodGridPatchParams from Dagor Engine (MIT) so that
	 * vertex shader morph logic can be ported directly.
	 */
	struct TerrainPatch
	{
		float    mOriginX     = 0.0f;
		float    mOriginZ     = 0.0f;
		float    mPatchSize   = 0.0f;  // world-space side length
		uint32_t mMorphFlags  = 0;     // bit 0: enable morph, bits 1-4: edge winding flags
	};

	/**
	 * Output of a TerrainLodGrid cull pass for one frame.
	 * Owns the flat list of visible patches, sorted coarse-to-fine.
	 */
	struct TerrainLodCullResult
	{
		TArray<TerrainPatch> mPatches;           // all visible patches, ordered fine→coarse
		TArray<TerrainPatch> mEdgePatches;       // transition patches for crack prevention
		uint32_t             mLod0PatchCount = 0;
		uint32_t             mMorphStartIndex = 0; // patches at [mMorphStartIndex..] need vertex morph
		Vector2              mGridOrigin      = Vector2::kZero;
		float                mWorldToLod0Scale = 1.0f;

		void Clear()
		{
			mPatches.clear();
			mEdgePatches.clear();
			mLod0PatchCount   = 0;
			mMorphStartIndex  = 0;
		}
	};

	/**
	 * Geo-Clipmap LOD grid for terrain heightmap rendering.
	 *
	 * The terrain is divided into a camera-centred concentric ring structure.
	 * Each ring corresponds to a LOD level; ring 0 (innermost) is the finest,
	 * outer rings use progressively coarser tessellation. Patches at ring
	 * boundaries morph smoothly in the vertex shader to avoid LOD popping.
	 *
	 * The cull algorithm is inspired by Dagor Engine's cull_lod_grid() /
	 * cull_lod_grid3() (MIT license), but re-implemented using B3D types.
	 *
	 * Vertex data is implicit (shared quad-strip index buffer); patch positions
	 * and morph parameters are passed as per-instance data in a GPU buffer.
	 */
	class B3D_EXPORT TerrainLodGrid
	{
	public:
		static constexpr uint32_t kMaxLods      = 16;
		static constexpr uint32_t kDefaultDim   = 32; // vertices per patch edge

		/** Configuration for the LOD grid. */
		struct Settings
		{
			/** Number of LOD levels (rings). Valid range: [1, kMaxLods]. */
			uint32_t mLodCount     = 8;

			/** Vertex count per patch edge (power of 2). Dagor default: 32. */
			uint32_t mPatchDim     = kDefaultDim;

			/** World-space size of the finest LOD patch in meters. */
			float mLod0PatchSize   = 16.0f;

			/**
			 * Fraction of each patch's extent at which morphing begins (0..1).
			 * Dagor uses ~0.6. Higher = starts morphing further from the boundary.
			 */
			float mMorphStart      = 0.6f;

			/** Size (in patches) of the LOD 0 zone radius. */
			uint32_t mLod0Radius   = 2;
		};

		TerrainLodGrid() = default;
		~TerrainLodGrid() { Close(); }

		TerrainLodGrid(const TerrainLodGrid&) = delete;
		TerrainLodGrid& operator=(const TerrainLodGrid&) = delete;

		/**
		 * Initialises the LOD grid with the supplied heightmap and settings.
		 * Builds patch-level min/max height cache from the heightmap hierarchy.
		 */
		bool Init(const Settings& settings, HeightmapData* heightmap);

		/** Releases all resources. */
		void Close();

		/**
		 * Culls the LOD grid against the current frustum and fills outResult
		 * with the list of visible patches for this frame.
		 *
		 * Thread-safe: may be called from a job thread. The result is written
		 * to outResult without touching any shared state.
		 *
		 * @param frustum       View frustum in world space.
		 * @param viewPos       Camera world position.
		 * @param outResult     Receives the sorted list of visible patches.
		 */
		void Cull(const ConvexVolume& frustum, Vector3 viewPos, TerrainLodCullResult& outResult) const;

		uint32_t GetPatchDim()  const { return mSettings.mPatchDim; }
		uint32_t GetLodCount()  const { return mSettings.mLodCount; }
		float    GetLod0Size()  const { return mSettings.mLod0PatchSize; }

	private:
		/** Cached min/max world height per patch at each LOD level. Used for AABB culling. */
		struct PatchHeightCache
		{
			TArray<Vector2> mMinMax;    // [patchIndex] = {worldMin, worldMax}
			uint32_t        mStride = 0;
			float           mPatchWorldSize = 0.0f;
		};

		/** Builds PatchHeightCache for all LOD levels from the heightmap data. */
		void BuildHeightCache();

		/**
		 * Generates visible patches for one LOD ring and appends them to outResult.
		 *
		 * Corresponds to one iteration of the ring loop in Dagor's cull_lod_grid().
		 */
		void CullLodRing(uint32_t lodLevel, Vector2 gridOriginXZ,
			const ConvexVolume& frustum, TerrainLodCullResult& outResult) const;

		/** Returns true if the patch AABB is inside the frustum. */
		bool IsPatchVisible(const ConvexVolume& frustum, float patchX, float patchZ,
			float patchSize, float hMin, float hMax) const;

		/** Returns the morph flags for a patch given its LOD ring and position. */
		uint32_t ComputeMorphFlags(uint32_t lodLevel, int32_t patchGridX, int32_t patchGridZ) const;

		Settings              mSettings;
		HeightmapData*        mHeightmap = nullptr;

		TArray<PatchHeightCache> mHeightCache; // one entry per LOD level
	};
}
