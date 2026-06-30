//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
//
// Fase 1: Implementação mínima de TerrainVirtualTexture que compila.
// A virtual texture (clipmap) completa é uma feature de Fase posterior (roadmap Fase 3/5).
// Mantemos a API do header intacta para compatibilidade futura.
#include "Terrain/B3DTerrainVirtualTexture.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "B3DApplication.h"
#include "Image/B3DPixelData.h"
#include "Math/B3DMath.h"
#include "Utility/B3DBitwise.h"
#include "Debug/B3DDebug.h"

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	bool TerrainVirtualTexture::Init(const Settings& settings, const PixelFormat* layers, uint32_t layerCount)
	{
		B3D_ASSERT(layerCount >= 1 && layerCount <= kMaxLayers);
		B3D_ASSERT(settings.mTileSize >= 4 && Bitwise::IsPow2(settings.mTileSize));
		B3D_ASSERT(settings.mMipCount >= 1);

		Close();
		mSettings   = settings;
		mLayerCount = layerCount;
		const uint32_t fullTile = settings.mTileSize + 2 * kTileBorder;

		TShared<GpuDevice> gpuDevice = GetApplication().GetPrimaryGpuDevice();
		if (!gpuDevice)
		{
			B3D_LOG(Log, LogTerrain,
				"TerrainVirtualTexture: no GPU device available — running in headless/no-op mode.");
			return true;
		}

		// ---- Atlas textures (one per layer) ----
		for (uint32_t l = 0; l < layerCount; ++l)
		{
			TextureCreateInformation texCI;
			texCI.Type        = TEX_TYPE_2D;
			texCI.Format      = layers[l];
			texCI.Width       = settings.mAtlasWidth;
			texCI.Height      = settings.mAtlasHeight;
			texCI.MipMapCount = 0;
			texCI.Usage       = TextureUsageFlag::Default | TextureUsageFlag::RenderTarget;
			texCI.Name        = String("TerrainAtlasLayer") + ToString(l);

			mAtlas[l] = Texture::Create(texCI);
		}

		// ---- Indirection texture (RG16U — atlas slot XY per virtual page) ----
		{
			TextureCreateInformation texCI;
			texCI.Type        = TEX_TYPE_2D;
			texCI.Format      = PF_RG16U;
			texCI.Width       = 1024;
			texCI.Height      = 1024;
			texCI.MipMapCount = settings.mMipCount;
			texCI.Usage       = TextureUsageFlag::Default;
			texCI.Name        = "TerrainIndirection";

			mIndirectionTex = Texture::Create(texCI);
		}

		// ---- Compute atlas slot grid dimensions ----
		mAtlasSlotsX = settings.mAtlasWidth  / fullTile;
		mAtlasSlotsY = settings.mAtlasHeight / fullTile;
		mTotalSlots  = mAtlasSlotsX * mAtlasSlotsY;
		B3D_ASSERT(mTotalSlots > 0);

		mAtlasSlots.Resize(mTotalSlots);
		for (uint32_t y = 0; y < mAtlasSlotsY; ++y)
			for (uint32_t x = 0; x < mAtlasSlotsX; ++x)
			{
				const uint32_t i = y * mAtlasSlotsX + x;
				mAtlasSlots[i].mAtlasX        = (uint16_t)(x * fullTile);
				mAtlasSlots[i].mAtlasY        = (uint16_t)(y * fullTile);
				mAtlasSlots[i].mLastUsedFrame = 0;
				mAtlasSlots[i].mIsDirty       = true;
			}

		B3D_LOG(Log, LogTerrain,
			"TerrainVirtualTexture: atlas={0}x{1}, tile={2}, slots={3}, mips={4}, layers={5} (software feedback stub)",
			settings.mAtlasWidth, settings.mAtlasHeight,
			settings.mTileSize, mTotalSlots, settings.mMipCount, layerCount);

		return true;
	}

	void TerrainVirtualTexture::Close()
	{
		for (uint32_t l = 0; l < kMaxLayers; ++l) mAtlas[l] = nullptr;
		mIndirectionTex = nullptr;
		mAtlasSlots.Clear();
		mTileToSlot.clear();
		mDirtyTiles.Clear();
		mNeededTiles.Clear();
		mLayerCount = 0;
	}

	void TerrainVirtualTexture::PrepareFeedback(Vector3 /*viewPos*/, const Matrix4& /*viewProjMatrix*/)
	{
		// Fase 1: sem feedback ainda. Apenas avança o frame counter.
		++mCurrentFrame;
	}

	void TerrainVirtualTexture::UpdateTiles(ITerrainSplatRenderer& /*splatRenderer*/)
	{
		// Fase 1: nenhum tile é renderizado. O terrain usa uma textura simples diretamente.
	}

	Area2 TerrainVirtualTexture::TileToWorldBounds(const TileAddress& tile) const
	{
		const float texelSize = mSettings.mStartTexelSize * (float)(1u << tile.mMip);
		const float tileW     = texelSize * mSettings.mTileSize;

		return Area2((int)(tile.mTileX * tileW), (int)(tile.mTileZ * tileW),
			(uint32_t)tileW, (uint32_t)tileW);
	}

	uint32_t TerrainVirtualTexture::EvictLRUSlot()
	{
		uint32_t lruSlot  = 0;
		uint32_t lruFrame = UINT32_MAX;

		for (uint32_t i = 0; i < mTotalSlots; ++i)
		{
			if (mAtlasSlots[i].mLastUsedFrame < lruFrame)
			{
				lruFrame = mAtlasSlots[i].mLastUsedFrame;
				lruSlot  = i;
			}
		}

		for (auto it = mTileToSlot.begin(); it != mTileToSlot.end(); ++it)
		{
			if (it->second == lruSlot)
			{
				mTileToSlot.erase(it);
				break;
			}
		}

		return lruSlot;
	}

	void TerrainVirtualTexture::RenderTile(const TileAddress& /*tile*/, uint32_t /*slotIndex*/,
		ITerrainSplatRenderer& /*splatRenderer*/)
	{
		// Stub — implementação real em Fase 3.
	}

	void TerrainVirtualTexture::UpdateIndirection(const TileAddress& /*tile*/, const AtlasSlot& /*slot*/)
	{
		// Stub — implementação real em Fase 3.
	}

	void TerrainVirtualTexture::InvalidateBox(const Area2& /*worldBoundsXZ*/)
	{
		// Stub — sem cache para invalidar na Fase 1.
	}

	void TerrainVirtualTexture::InvalidateAll()
	{
		mTileToSlot.clear();
		mDirtyTiles.Clear();
	}

	void TerrainVirtualTexture::BindForRendering() const
	{
		// A binding real acontece via TerrainMaterial (Fase 3).
	}

	HTexture TerrainVirtualTexture::GetAtlasTexture(uint32_t layerIndex) const
	{
		B3D_ASSERT(layerIndex < mLayerCount);
		return mAtlas[layerIndex];
	}

	HTexture TerrainVirtualTexture::GetIndirectionTexture() const
	{
		return mIndirectionTex;
	}
}