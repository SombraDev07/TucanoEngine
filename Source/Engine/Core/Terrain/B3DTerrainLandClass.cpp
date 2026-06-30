//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Terrain/B3DTerrainLandClass.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "B3DApplication.h"
#include "Image/B3DPixelData.h"
#include "Math/B3DMath.h"
#include "Debug/B3DDebug.h"

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	// -----------------------------------------------------------------------
	// TerrainLandClassManager
	// -----------------------------------------------------------------------

	bool TerrainLandClassManager::Init(uint32_t splatMaskResolution)
	{
		Close();

		mSplatMaskRes = splatMaskResolution;
		const uint32_t numPixels = splatMaskResolution * splatMaskResolution;
		mSplatMaskData.Resize(numPixels * 4);
		std::memset(mSplatMaskData.Data(), 0, numPixels * 4);

		// Default: land class 0 = weight 1.0 everywhere (channel R = 255)
		for (uint32_t i = 0; i < numPixels; ++i)
			mSplatMaskData[i * 4 + 0] = 255;

		// Create GPU texture
		{
			TextureCreateInformation ci;
			ci.Type          = TEX_TYPE_2D;
			ci.Format        = PF_RGBA8;
			ci.Width         = splatMaskResolution;
			ci.Height        = splatMaskResolution;
			ci.MipMapCount   = 0;
			ci.Usage         = TextureUsageFlag::Default;
			ci.Name          = "TerrainSplatMask";

			auto pixelData = B3DMakeShared<PixelData>(splatMaskResolution, splatMaskResolution, 1, PF_RGBA8);
			std::memcpy(pixelData->GetData(), mSplatMaskData.Data(), mSplatMaskData.Size());

			ci.InitialData     = pixelData;
			mSplatMaskTex      = Texture::Create(ci);
		}

		B3D_LOG(Log, LogTerrain,
			"TerrainLandClassManager: splat mask {0}x{0}", splatMaskResolution);
		return true;
	}

	void TerrainLandClassManager::Close()
	{
		mLandClasses.Clear();
		mSplatMaskData.Clear();
		mSplatMaskTex = nullptr;
		mSplatMaskRes = 0;
	}

	uint32_t TerrainLandClassManager::AddLandClass(const TerrainLandClass& landClass)
	{
		B3D_ASSERT(mLandClasses.Size() < kMaxLandClasses);
		mLandClasses.Add(landClass);
		return (uint32_t)mLandClasses.Size() - 1;
	}

	const TerrainLandClass* TerrainLandClassManager::GetLandClass(uint32_t index) const
	{
		if (index >= mLandClasses.Size()) return nullptr;
		return &mLandClasses[index];
	}

	void TerrainLandClassManager::Paint(uint32_t landClassIndex, Vector2 worldXZ,
		float radiusMeters, float weight,
		Vector2 terrainOrigin, float terrainSizeMeters)
	{
		if (mSplatMaskRes == 0) return;

		// Channel index for this land class (0..3 = R,G,B,A)
		const uint32_t channel = landClassIndex & 3u;

		const float texelSize = terrainSizeMeters / (float)mSplatMaskRes;
		const float radiusTexels = radiusMeters / texelSize;

		// Convert world XZ -> texel centre
		const float cx = (worldXZ.X - terrainOrigin.X) / texelSize;
		const float cz = (worldXZ.Y - terrainOrigin.Y) / texelSize;

		const int32_t x0 = Math::Max(0, (int32_t)(cx - radiusTexels));
		const int32_t x1 = Math::Min((int32_t)mSplatMaskRes - 1, (int32_t)(cx + radiusTexels + 1));
		const int32_t z0 = Math::Max(0, (int32_t)(cz - radiusTexels));
		const int32_t z1 = Math::Min((int32_t)mSplatMaskRes - 1, (int32_t)(cz + radiusTexels + 1));

		const float r2 = radiusTexels * radiusTexels;

		for (int32_t z = z0; z <= z1; ++z)
			for (int32_t x = x0; x <= x1; ++x)
			{
				const float dx = x - cx, dz = z - cz;
				if (dx * dx + dz * dz > r2) continue;

				const uint32_t pixelBase = ((uint32_t)z * mSplatMaskRes + (uint32_t)x) * 4;

				// Soft-brush falloff
				const float dist   = std::sqrt(dx * dx + dz * dz);
				const float falloff = 1.0f - Math::Clamp01(dist / radiusTexels);
				const float alpha  = weight * falloff;

				uint8_t& c = mSplatMaskData[pixelBase + channel];
				c = (uint8_t)Math::Min(255u, (uint32_t)(c + (uint32_t)(alpha * 255.0f)));
			}
	}

	void TerrainLandClassManager::UploadSplatMask()
	{
		if (!mSplatMaskTex || mSplatMaskData.Empty()) return;

		auto pixelData = B3DMakeShared<PixelData>(mSplatMaskRes, mSplatMaskRes, 1, PF_RGBA8);
		std::memcpy(pixelData->GetData(), mSplatMaskData.Data(), mSplatMaskData.Size());
		mSplatMaskTex->WriteData(pixelData);
	}

	// -----------------------------------------------------------------------
	// TerrainDefaultSplatRenderer
	// -----------------------------------------------------------------------

	bool TerrainDefaultSplatRenderer::Init(TerrainLandClassManager* landClasses)
	{
		B3D_ASSERT(landClasses);
		mLandClasses = landClasses;
		return true;
	}

	void TerrainDefaultSplatRenderer::OnStartRenderTiles(Vector2 /*viewCenter*/)
	{
		// Stub — full implementation requires a splat material (Fase 3).
	}

	void TerrainDefaultSplatRenderer::OnRenderTile(const Area2& /*regionXZ*/)
	{
		// Stub — full implementation requires a splat material (Fase 3).
	}

	void TerrainDefaultSplatRenderer::OnEndRenderTiles()
	{
		// No-op for now.
	}
}