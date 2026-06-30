//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Terrain/B3DTerrainSystem.h"
#include "Terrain/B3DTerrainRenderer.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "B3DApplication.h"
#include "Components/B3DCamera.h"
#include "Scene/B3DSceneObject.h"
#include "Image/B3DPixelData.h"
#include "FileSystem/B3DFileSystem.h"
#include "Math/B3DMath.h"
#include "Debug/B3DDebug.h"

#include <cmath>
#include <cstring>

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	TerrainSystem::TerrainSystem() = default;
	TerrainSystem::~TerrainSystem() { Shutdown(); }

	// -----------------------------------------------------------------------
	// Init
	// -----------------------------------------------------------------------
	bool TerrainSystem::Init(const Settings& settings)
	{
		B3D_ASSERT(!mIsInitialised);

		// ---- Heightmap ----
		mHeightmap = B3DMakeUnique<HeightmapData>();
		if (!LoadHeightmap(settings))
		{
			B3D_LOG(Warning, LogTerrain,
				"TerrainSystem: failed to load heightmap from '{0}'", settings.mHeightmapPath);
			return false;
		}

		// ---- LOD grid ----
		mLodGrid = B3DMakeUnique<TerrainLodGrid>();
		if (!mLodGrid->Init(settings.mLodSettings, mHeightmap.get()))
			return false;

		// ---- Virtual Texture ----
		if (settings.mVTexLayerCount > 0)
		{
			mVirtualTexture = B3DMakeUnique<TerrainVirtualTexture>();
			if (!mVirtualTexture->Init(settings.mVTexSettings, settings.mVTexLayers, settings.mVTexLayerCount))
				return false;
		}

		// ---- Renderer ----
		TShared<GpuDevice> gpuDevice = GetApplication().GetPrimaryGpuDevice();
		if (gpuDevice)
		{
			mRenderer = B3DMakeUnique<render::TerrainRenderer>();
			if (!mRenderer->Init(settings.mLodSettings.mPatchDim, *gpuDevice))
				return false;
		}

		mIsInitialised = true;
		B3D_LOG(Log, LogTerrain, "TerrainSystem: initialised successfully.");
		return true;
	}

	void TerrainSystem::Shutdown()
	{
		mRenderer.reset();
		mVirtualTexture.reset();
		mLodGrid.reset();
		mHeightmap.reset();
		mIsInitialised = false;
	}

	// -----------------------------------------------------------------------
	// Heightmap loading
	// -----------------------------------------------------------------------
	bool TerrainSystem::LoadHeightmap(const Settings& settings)
	{
		if (settings.mHeightmapPath.empty())
		{
			// Create a 512x512 procedural heightmap (sine hills) for testing.
			const uint32_t w = 512, h = 512;
			TArray<uint16_t> data;
			data.Resize(w * h);
			for (uint32_t z = 0; z < h; ++z)
				for (uint32_t x = 0; x < w; ++x)
				{
					const float fx = (float)x / (float)w;
					const float fz = (float)z / (float)h;
					const float hill = std::sin(fx * 6.28318f * 2.0f) * std::cos(fz * 6.28318f * 2.0f);
					const float height01 = 0.5f + 0.4f * hill;
					data[z * w + x] = (uint16_t)(height01 * 65535.0f);
				}
			return mHeightmap->Init(data.Data(), w, h,
				settings.mCellSizeMeters, settings.mHeightMin,
				settings.mHeightScale, settings.mWorldOffset);
		}

		// Try loading a raw 16-bit binary file (little-endian uint16, width*height samples)
		Path path(settings.mHeightmapPath);
		if (!FileSystem::IsFile(path))
		{
			B3D_LOG(Warning, LogTerrain, "Heightmap file not found: {0}", settings.mHeightmapPath);
			return false;
		}

		TShared<DataStream> stream = FileSystem::OpenFile(path);
		if (!stream) return false;

		const uint64_t byteSize = stream->Size();
		const uint32_t numSamples = (uint32_t)(byteSize / sizeof(uint16_t));
		const uint32_t side = (uint32_t)std::sqrt((double)numSamples);

		if ((uint64_t)side * side != numSamples)
		{
			B3D_LOG(Warning, LogTerrain,
				"Heightmap '{0}': size {1} bytes is not a square uint16 map.", settings.mHeightmapPath, byteSize);
			return false;
		}

		TArray<uint16_t> rawData;
		rawData.Resize(numSamples);
		stream->Read(rawData.Data(), byteSize);

		return mHeightmap->Init(rawData.Data(), side, side,
			settings.mCellSizeMeters, settings.mHeightMin,
			settings.mHeightScale, settings.mWorldOffset);
	}

	// -----------------------------------------------------------------------
	// Per-frame update
	// -----------------------------------------------------------------------
	void TerrainSystem::Update(const Camera& camera)
	{
		if (!mIsInitialised) return;

		Vector3 viewPos  = camera.SO()->GetTransform().GetPosition();
		const ConvexVolume& frustum = camera.GetFrustum();

		// Cull LOD grid -> visible patch list
		mLodGrid->Cull(frustum, viewPos, mCullResult);

		// Update virtual texture feedback
		if (mVirtualTexture)
		{
			const Matrix4 viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
			mVirtualTexture->PrepareFeedback(viewPos, viewProj);

			if (mSplatRenderer)
				mVirtualTexture->UpdateTiles(*mSplatRenderer);
		}
	}

	// -----------------------------------------------------------------------
	// Rendering
	// -----------------------------------------------------------------------
	void TerrainSystem::Render(const Camera& camera)
	{
		if (!mIsInitialised || !mRenderer) return;

		// Fase 1: the render-thread material integration (RendererMaterial) is compiled but
		// not driven from here yet. The Terrain.exe example renders the terrain via a
		// SceneObject/Renderable + standard material for immediate visual validation.
		// Fase 2 will wire the render-thread TerrainRenderer into the render compositor.
		B3D_LOG(Log, LogTerrain,
			"TerrainSystem::Render - {0} patches culled", mCullResult.mPatches.Size());
	}

	void TerrainSystem::RenderDepth(const Camera& camera)
	{
		if (!mIsInitialised || !mRenderer) return;

		B3D_LOG(Log, LogTerrain,
			"TerrainSystem::RenderDepth - {0} patches culled", mCullResult.mPatches.Size());
	}

	// -----------------------------------------------------------------------
	// Splat renderer registration
	// -----------------------------------------------------------------------
	void TerrainSystem::SetSplatRenderer(ITerrainSplatRenderer* splatRenderer)
	{
		mSplatRenderer = splatRenderer;
	}

	// -----------------------------------------------------------------------
	// Physics / gameplay queries
	// -----------------------------------------------------------------------
	bool TerrainSystem::GetHeightAtPoint(Vector2 worldXZ, float& outHeight) const
	{
		if (!mHeightmap || !mHeightmap->IsValid()) return false;

		const AABox wb = mHeightmap->GetWorldBounds();
		if (worldXZ.X < wb.Minimum.X || worldXZ.Y < wb.Minimum.Z ||
			worldXZ.X > wb.Maximum.X || worldXZ.Y > wb.Maximum.Z)
			return false;

		outHeight = mHeightmap->GetHeightAtWorld(worldXZ.X, worldXZ.Y);
		return true;
	}

	bool TerrainSystem::GetHeightAndNormal(Vector2 worldXZ, float& outHeight, Vector3& outNormal) const
	{
		if (!mHeightmap || !mHeightmap->IsValid()) return false;
		return mHeightmap->GetHeightAndNormal(worldXZ, outHeight, outNormal);
	}

	float TerrainSystem::TraceRay(const Ray& ray, float maxDist) const
	{
		if (!mHeightmap || !mHeightmap->IsValid()) return -1.0f;
		return mHeightmap->TraceRay(ray.Origin, ray.Direction, maxDist);
	}
}