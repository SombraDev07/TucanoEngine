//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Utility/B3DModule.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DMatrix4.h"
#include "Math/B3DConvexVolume.h"
#include "Math/B3DRay.h"
#include "Terrain/B3DHeightmapData.h"
#include "Terrain/B3DTerrainLodGrid.h"
#include "Terrain/B3DTerrainVirtualTexture.h"

namespace b3d
{
	class Camera;
	class ITerrainSplatRenderer;
	namespace render { class TerrainRenderer; }

	/**
	 * Central singleton module that owns all terrain subsystems.
	 *
	 * Drives the terrain render pipeline each frame and exposes a unified API
	 * for gameplay queries (height, normal, raycast).
	 *
	 * Usage:
	 * @code
	 *   // Startup
	 *   TerrainSystem::StartUp();
	 *   TerrainSystem::Instance().Init(settings);
	 *
	 *   // Per frame
	 *   TerrainSystem::Instance().Update(camera);
	 *   TerrainSystem::Instance().Render(camera);
	 *
	 *   // Shutdown
	 *   TerrainSystem::ShutDown();
	 * @endcode
	 */
	class B3D_EXPORT TerrainSystem : public Module<TerrainSystem>
	{
	public:
		/** Combined init settings for all terrain subsystems. */
		struct Settings
		{
			// ---- Heightmap ----

			/** Path to the terrain heightmap asset. */
			String mHeightmapPath;

			/** World-space size of one heightmap texel (meters). */
			float mCellSizeMeters = 1.0f;

			/** World-space height at raw value 0. */
			float mHeightMin = 0.0f;

			/** World-space height range (maxHeight - minHeight). */
			float mHeightScale = 512.0f;

			/** World-space XZ position of the heightmap [0,0] corner. */
			Vector2 mWorldOffset = Vector2::kZero;

			// ---- LOD grid ----
			TerrainLodGrid::Settings mLodSettings;

			// ---- Virtual texture ----
			TerrainVirtualTexture::Settings mVTexSettings;
			PixelFormat mVTexLayers[TerrainVirtualTexture::kMaxLayers] = {};
			uint32_t    mVTexLayerCount = 0;
		};

		TerrainSystem();
		~TerrainSystem() override;

		/**
		 * Loads the heightmap and initialises all subsystems.
		 * Must be called before any other method.
		 */
		bool Init(const Settings& settings);

		/** Tears down and releases all terrain resources. */
		void Shutdown();

		// ---- Per-frame update ----

		/**
		 * Culls the LOD grid against the camera frustum and prepares
		 * virtual texture feedback. Call before Render().
		 */
		void Update(const Camera& camera);

		/** Submits terrain geometry and material draw calls. */
		void Render(const Camera& camera);

		/** Renders depth-only (shadow maps, depth pre-pass). */
		void RenderDepth(const Camera& camera);

		/**
		 * Registers the splat renderer used to fill virtual texture tiles.
		 * The renderer must remain valid for the lifetime of the TerrainSystem.
		 */
		void SetSplatRenderer(ITerrainSplatRenderer* splatRenderer);

		// ---- Gameplay / Physics queries ----

		/**
		 * Returns the terrain height at a world-space XZ position.
		 * @return False if the position is outside the terrain bounds.
		 */
		bool GetHeightAtPoint(Vector2 worldXZ, float& outHeight) const;

		/**
		 * Returns the terrain height and surface normal at a world-space XZ position.
		 * @return False if outside terrain bounds.
		 */
		bool GetHeightAndNormal(Vector2 worldXZ, float& outHeight, Vector3& outNormal) const;

		/**
		 * Traces a ray against the terrain and returns the hit distance.
		 * @return Hit distance in [0, maxDist], or -1 if no intersection.
		 */
		float TraceRay(const Ray& ray, float maxDist) const;

		// ---- Accessors ----

		HeightmapData*         GetHeightmapData()  const { return mHeightmap.get(); }
		TerrainLodGrid*        GetLodGrid()         const { return mLodGrid.get(); }
		TerrainVirtualTexture* GetVirtualTexture()  const { return mVirtualTexture.get(); }

		bool IsInitialised() const { return mIsInitialised; }

	private:
		/** Loads a raw 16-bit PNG heightmap into HeightmapData. */
		bool LoadHeightmap(const Settings& settings);

		TUnique<HeightmapData>         mHeightmap;
		TUnique<TerrainLodGrid>        mLodGrid;
		TUnique<TerrainVirtualTexture> mVirtualTexture;
		TUnique<render::TerrainRenderer> mRenderer;
		ITerrainSplatRenderer*         mSplatRenderer = nullptr;

		TerrainLodCullResult           mCullResult;  // re-used each frame (avoids allocation)
		bool                           mIsInitialised = false;
	};
}
