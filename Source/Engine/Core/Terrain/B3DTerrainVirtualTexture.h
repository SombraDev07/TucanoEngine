//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DMatrix4.h"
#include "Math/B3DArea2.h"
#include "Image/B3DTexture.h"

namespace b3d
{
	class Camera;

	/**
	 * Interface for rendering terrain material (splat) into a clipmap tile region.
	 *
	 * Implement this to feed your material / biome painting system into the
	 * virtual texture tile atlas. Equivalent to Dagor's ClipmapRenderer interface.
	 */
	class B3D_EXPORT ITerrainSplatRenderer
	{
	public:
		virtual ~ITerrainSplatRenderer() = default;

		/** Called before any tiles are rendered this frame. */
		virtual void OnStartRenderTiles(Vector2 viewCenter) = 0;

		/**
		 * Renders splat/material data into a world-space region.
		 * The output should be written to the currently bound render target
		 * (already set up by TerrainVirtualTexture before this call).
		 */
		virtual void OnRenderTile(const Area2& regionXZ) = 0;

		/** Called after all tiles for this frame have been rendered. */
		virtual void OnEndRenderTiles() = 0;
	};

	/**
	 * Terrain Virtual Texture (Clipmap) system.
	 *
	 * Implements a sparse virtual texturing system for terrain surface appearance.
	 * The terrain surface is logically divided into tiles at multiple mip levels.
	 * Only tiles visible from the current camera are rendered (via ITerrainSplatRenderer)
	 * and cached in a physical GPU atlas. An indirection texture maps virtual tile
	 * coordinates to physical atlas slots.
	 *
	 * Architecture based on Dagor Engine's Clipmap / ClipmapImpl (MIT license):
	 * - Feedback determines which tiles are needed each frame (software path).
	 * - LRU eviction removes unused tiles from the atlas.
	 * - The indirection texture (uint16 XY) is updated incrementally.
	 *
	 * Texture layers:
	 *   0 - Albedo       (RGBA8)
	 *   1 - Normal       (RG16F or BC5)
	 *   2 - Roughness/AO (RG8)
	 */
	class B3D_EXPORT TerrainVirtualTexture
	{
	public:
		static constexpr uint32_t kMaxLayers     = 4;
		static constexpr uint32_t kTileBorder    = 4;    // border texels for bilinear filter
		static constexpr uint32_t kDefaultTileSize = 256; // inner tile texels

		/** Feedback mode for determining which tiles to upload. */
		enum class FeedbackMode
		{
			/** CPU computes visible tiles from camera frusta (fast, approximate). */
			kSoftwareCPU,

			/**
			 * GPU writes tile requests to a UAV buffer during the feedback pass.
			 * More accurate but requires an extra render pass.
			 * (Equivalent to Dagor's CPU_HW_FEEDBACK mode.)
			 */
			kGpuUAV,
		};

		struct Settings
		{
			/** Physical atlas dimensions. Larger = more cached tiles. */
			uint32_t mAtlasWidth  = 4096;
			uint32_t mAtlasHeight = 4096;

			/** Inner tile texel size (power of 2). Total tile = mTileSize + 2*kTileBorder. */
			uint32_t mTileSize = kDefaultTileSize;

			/** Number of virtual mip levels (Dagor default: 6). */
			uint32_t mMipCount = 6;

			/** Finest mip texel-to-meter ratio. */
			float mStartTexelSize = 0.05f;

			/** Max tiles uploaded to atlas per frame. */
			uint32_t mMaxTileUpdatesPerFrame = 8;

			FeedbackMode mFeedbackMode = FeedbackMode::kSoftwareCPU;
		};

		TerrainVirtualTexture() = default;
		~TerrainVirtualTexture() { Close(); }

		TerrainVirtualTexture(const TerrainVirtualTexture&) = delete;
		TerrainVirtualTexture& operator=(const TerrainVirtualTexture&) = delete;

		/**
		 * Initialises the atlas textures and indirection texture.
		 * @param settings  Configuration.
		 * @param layers    GPU texture formats for each atlas layer (e.g. PF_BYTE_RGBA).
		 * @param layerCount Number of layers (≤ kMaxLayers).
		 */
		bool Init(const Settings& settings, const PixelFormat* layers, uint32_t layerCount);

		/** Releases all GPU resources. */
		void Close();

		/**
		 * Analyses the view frustum and marks which tiles need to be rendered.
		 * Call once per frame before UpdateTiles().
		 */
		void PrepareFeedback(Vector3 viewPos, const Matrix4& viewProjMatrix);

		/**
		 * Renders dirty tiles using the supplied splat renderer callback and
		 * uploads them to the atlas. Respects mMaxTileUpdatesPerFrame.
		 */
		void UpdateTiles(ITerrainSplatRenderer& splatRenderer);

		/** Marks all tiles overlapping a world-space box as dirty (forces re-render). */
		void InvalidateBox(const Area2& worldBoundsXZ);

		/** Invalidates all tiles and forces a full redraw next frame. */
		void InvalidateAll();

		/** Binds atlas textures and the indirection texture to global shader vars. */
		void BindForRendering() const;

		HTexture GetAtlasTexture(uint32_t layerIndex) const;
		HTexture GetIndirectionTexture() const;

		uint32_t GetTileSize()   const { return mSettings.mTileSize; }
		uint32_t GetMipCount()   const { return mSettings.mMipCount; }
		float    GetTexelSize()  const { return mSettings.mStartTexelSize; }

	private:
		/** Uniquely identifies one virtual texture tile (mip + grid coords). */
		struct TileAddress
		{
			uint8_t  mMip    = 0;
			uint16_t mTileX  = 0;
			uint16_t mTileZ  = 0;

			uint64_t ToKey() const
			{
				return ((uint64_t)mMip << 32) | ((uint64_t)mTileX << 16) | mTileZ;
			}
		};

		/** State of one physical atlas slot. */
		struct AtlasSlot
		{
			uint16_t mAtlasX         = 0;
			uint16_t mAtlasY         = 0;
			uint32_t mLastUsedFrame  = 0;
			bool     mIsDirty        = true;
		};

		/** Converts tile address to world-space bounding box for splat rendering. */
		Area2 TileToWorldBounds(const TileAddress& tile) const;

		/** Selects the LRU slot to evict when the atlas is full. */
		uint32_t EvictLRUSlot();

		/** Renders one tile and copies it to the atlas. */
		void RenderTile(const TileAddress& tile, uint32_t slotIndex, ITerrainSplatRenderer& splatRenderer);

		/** Updates the indirection texture for one tile. */
		void UpdateIndirection(const TileAddress& tile, const AtlasSlot& slot);

		// ---- Software feedback ----
		void CollectVisibleTiles(Vector3 viewPos, const Matrix4& viewProjMatrix,
			TArray<TileAddress>& outNeeded) const;

		Settings              mSettings;
		uint32_t              mLayerCount = 0;
		HTexture              mAtlas[kMaxLayers];
		HTexture              mIndirectionTex;

		TArray<AtlasSlot>           mAtlasSlots;
		UnorderedMap<uint64_t, uint32_t> mTileToSlot;  // tile key → slot index
		TArray<TileAddress>         mDirtyTiles;
		TArray<TileAddress>         mNeededTiles;  // refreshed by feedback each frame

		uint32_t mCurrentFrame   = 0;
		uint32_t mAtlasSlotsX    = 0;
		uint32_t mAtlasSlotsY    = 0;
		uint32_t mTotalSlots     = 0;
	};
}
