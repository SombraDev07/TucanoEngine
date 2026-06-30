//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Math/B3DVector2.h"
#include "Math/B3DArea2.h"
#include "Image/B3DTexture.h"
#include "Terrain/B3DTerrainVirtualTexture.h"

namespace b3d
{
	/**
	 * Describes one land class (material region) of the terrain.
	 *
	 * A land class is a painted region that maps to a set of detail textures.
	 * The splatting shader blends up to kMaxLayersPerClass detail textures using a
	 * per-pixel weight computed from altitude, slope, and the splat mask.
	 *
	 * Conceptually equivalent to Dagor's LandClassDetailTextures and biomeQuery.
	 */
	struct TerrainLandClass
	{
		static constexpr uint32_t kMaxLayersPerClass = 4;

		/** Human-readable name (e.g. "Grass", "Rock", "Sand"). */
		String mName;

		/** World-space altitude range in which this land class is blended in. */
		float mAltitudeMin = -FLT_MAX;
		float mAltitudeMax =  FLT_MAX;

		/** Normal.y range (dot with up) in which this land class is blended in. */
		float mSlopeMin = 0.0f;
		float mSlopeMax = 1.0f;

		/** Detail texture layers for this land class. */
		struct Layer
		{
			HTexture mAlbedo;    // RGB8 or BC1
			HTexture mNormal;    // RG16F or BC5
			HTexture mRoughAO;   // RG8
			float    mUVScale = 1.0f; // texture tiling scale in world-space meters
		};

		Layer    mLayers[kMaxLayersPerClass];
		uint32_t mLayerCount = 0;
	};

	/**
	 * Manages the collection of land class definitions and the per-pixel splat mask texture.
	 *
	 * The splat mask is a single RGBA8 texture covering the entire terrain; each channel
	 * stores the weight of one land class (up to 4 land classes supported simultaneously).
	 * Land classes beyond 4 require a second splat pass.
	 */
	class B3D_EXPORT TerrainLandClassManager
	{
	public:
		static constexpr uint32_t kMaxLandClasses = 32;

		TerrainLandClassManager() = default;
		~TerrainLandClassManager() { Close(); }

		/**
		 * Initialises the land class system.
		 * @param splatMaskResolution  Resolution of the per-terrain splat mask in texels.
		 */
		bool Init(uint32_t splatMaskResolution = 2048);

		void Close();

		/** Adds a new land class and returns its index. */
		uint32_t AddLandClass(const TerrainLandClass& landClass);

		/** Returns the land class at the given index. */
		const TerrainLandClass* GetLandClass(uint32_t index) const;

		/**
		 * Paints the splat mask with the given land class at a world-space position.
		 * Weight 1.0 = fully this class, 0.0 = no change.
		 */
		void Paint(uint32_t landClassIndex, Vector2 worldXZ, float radiusMeters,
			float weight, Vector2 terrainOrigin, float terrainSizeMeters);

		/**
		 * Uploads the current splat mask CPU data to the GPU texture.
		 * Must be called after painting to see changes on GPU.
		 */
		void UploadSplatMask();

		HTexture GetSplatMaskTexture() const { return mSplatMaskTex; }
		uint32_t GetLandClassCount()   const { return (uint32_t)mLandClasses.size(); }

	private:
		TArray<TerrainLandClass> mLandClasses;
		TArray<uint8_t>          mSplatMaskData; // RGBA8 CPU-side paint buffer
		uint32_t                 mSplatMaskRes = 0;
		HTexture                 mSplatMaskTex;
	};

	/**
	 * Default implementation of ITerrainSplatRenderer.
	 *
	 * Renders the splat/material texture into virtual texture tiles using the
	 * TerrainSplatDefault.bsl shader. Samples land class detail textures and
	 * blends them according to the splat mask.
	 */
	class B3D_EXPORT TerrainDefaultSplatRenderer : public ITerrainSplatRenderer
	{
	public:
		TerrainDefaultSplatRenderer() = default;

		/**
		 * Initialises with a reference to the land class manager.
		 * The manager must outlive this renderer.
		 */
		bool Init(TerrainLandClassManager* landClasses);

		void OnStartRenderTiles(Vector2 viewCenter) override;
		void OnRenderTile(const Area2& regionXZ) override;
		void OnEndRenderTiles() override;

	private:
		TerrainLandClassManager* mLandClasses = nullptr;
	};
}
