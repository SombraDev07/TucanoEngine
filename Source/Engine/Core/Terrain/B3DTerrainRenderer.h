//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "GpuBackend/B3DVertexDescription.h"
#include "Math/B3DMatrix4.h"
#include "Math/B3DVector3.h"
#include "Math/B3DConvexVolume.h"
#include "Image/B3DTexture.h"
#include "Terrain/B3DTerrainLodGrid.h"

namespace b3d
{
	class HeightmapData;
	class Camera;

	namespace render
	{
		class TerrainMaterial;
		class TerrainDepthMaterial;

	/**
	 * Terrain GPU renderer.
	 *
	 * Owns:
	 * - The shared patch vertex buffer (a kPatchDim×kPatchDim quad-strip grid, indexed)
	 * - The shared patch index buffer
	 * - A per-frame instance data buffer (filled from TerrainLodCullResult each frame)
	 * - The RendererMaterial instances for the geometry and depth passes
	 *
	 * Call:
	 *   Init(patchDim)           once at startup
	 *   BeginFrame(camera, ...) every frame — uploads frame constants + patch instances
	 *   RenderGeometry(cb)      — geometry pass
	 *   RenderDepth(cb)         — depth/shadow pass
	 */
	class B3D_EXPORT TerrainRenderer
	{
	public:
		static constexpr uint32_t kMaxPatchesPerFrame = 16384;

		TerrainRenderer() = default;
		~TerrainRenderer() { Close(); }

		TerrainRenderer(const TerrainRenderer&) = delete;
		TerrainRenderer& operator=(const TerrainRenderer&) = delete;

		/**
		 * Creates the shared patch mesh and renderer materials.
		 *
		 * @param patchDim      Vertices per patch edge (power of 2, e.g. 32).
		 * @param gpuDevice     GPU device to allocate resources on.
		 */
		bool Init(uint32_t patchDim, GpuDevice& gpuDevice);

		/** Releases all GPU resources. */
		void Close();

		/**
		 * Prepares this frame's render state.
		 *
		 * Uploads the frame-constant uniform buffer and fills the instance buffer
		 * with visible patch data from the cull result.
		 *
		 * @param cullResult    Output of TerrainLodGrid::Cull().
		 * @param heightmap     Heightmap GPU texture (R16_UNORM).
		 * @param viewProjMat   Combined view-projection matrix.
		 * @param cameraPos     Camera world-space position.
		 * @param heightmapTex  Heightmap GPU texture (R16_UNORM).
		 * @param heightMin     World-space height at raw=0.
		 * @param heightScale   World-space height range (max - min).
		 * @param worldOffset   World-space XZ offset of the heightmap corner.
		 * @param worldSizeXZ   World-space XZ extents of the terrain.
		 * @param morphStart    Distance at which morph begins (fraction of patch size).
		 * @param indirectionTex Clipmap indirection texture (RG16U).
		 * @param atlasAlbedo   Clipmap atlas albedo layer.
		 * @param atlasNormal   Clipmap atlas normal layer.
		 * @param atlasRoughAO  Clipmap atlas roughness/AO layer.
		 * @param commandBuffer Transfer command buffer for staging uploads.
		 */
		void BeginFrame(
			const TerrainLodCullResult& cullResult,
			const TShared<Texture>& heightmapTex,
			const Matrix4&          viewProjMat,
			const Vector3&          cameraPos,
			float heightMin, float heightScale,
			Vector2 worldOffset, Vector2 worldSizeXZ,
			float morphStart,
			const TShared<Texture>& indirectionTex,
			const TShared<Texture>& atlasAlbedo,
			const TShared<Texture>& atlasNormal,
			const TShared<Texture>& atlasRoughAO,
			render::GpuCommandBuffer& commandBuffer);

		/**
		 * Submits all visible patches as instanced draw calls — geometry (colour) pass.
		 * Must be called inside a render pass.
		 */
		void RenderGeometry(render::GpuCommandBuffer& commandBuffer);

		/**
		 * Submits all visible patches — depth-only pass (shadows, prepass).
		 * Must be called inside a render pass.
		 */
		void RenderDepth(render::GpuCommandBuffer& commandBuffer);

		uint32_t GetPatchDim()      const { return mPatchDim; }
		uint32_t GetPatchCount()    const { return mPatchCount; }
		bool     IsInitialised()    const { return mIsInitialised; }

	private:
		/** Generates vertex positions (normalised [0,1] XZ) for a quad grid of patchDim×patchDim. */
		void BuildPatchMesh(uint32_t patchDim, GpuDevice& gpuDevice);

		/** Uploads patch instance data (origin, size, morph flags) to the GPU. */
		void UploadInstances(const TerrainLodCullResult& cullResult,
			render::GpuCommandBuffer& commandBuffer);

		/** Issues an instanced draw for all patches. */
		void DrawPatches(render::GpuCommandBuffer& commandBuffer) const;

		// Shared quad-grid geometry (one mesh per patchDim, reused for all LODs)
		TShared<GpuBuffer>          mVertexBuffer;
		TShared<GpuBuffer>          mIndexBuffer;
		TShared<VertexDescription>  mVertexDescription;
		uint32_t                    mIndexCount   = 0;
		uint32_t                    mPatchDim     = 0;

		// Per-frame instance buffer (TerrainPatch structs, one per patch)
		TShared<GpuBuffer>          mInstanceBuffer;   // GPU-side (StoreOnGPU)
		TShared<GpuBuffer>          mInstanceStaging;  // CPU-side staging

		// Deferred staging buffers for the shared patch mesh (uploaded on first BeginFrame)
		TShared<GpuBuffer>          mPendingVertexStaging;
		TShared<GpuBuffer>          mPendingIndexStaging;

		uint32_t                    mPatchCount   = 0; // patches submitted this frame

		// RendererMaterial instances (one per material type; accessed on render thread)
		TerrainMaterial*      mMaterial      = nullptr;
		TerrainDepthMaterial* mDepthMaterial = nullptr;

		bool                          mIsInitialised = false;
	};
	} // namespace render
} // namespace b3d
