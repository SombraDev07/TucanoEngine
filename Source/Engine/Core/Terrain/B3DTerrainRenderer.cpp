//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Terrain/B3DTerrainRenderer.h"
#include "Terrain/B3DTerrainMaterials.h"
#include "Terrain/B3DTerrainUniformBuffers.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "GpuBackend/B3DVertexDescription.h"
#include "Renderer/B3DGpuUniformBuffer.h"
#include "Utility/B3DBitwise.h"
#include "Math/B3DMath.h"
#include "Debug/B3DDebug.h"

namespace b3d
{
	namespace render
	{
		B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

		// Definitions for the extern uniform buffer singletons declared in the header.
		TerrainFrameParamsDef gTerrainFrameParams;
		TerrainPatchParamsDef gTerrainPatchParams;
		TerrainLightParamsDef gTerrainLightParams;

	// -----------------------------------------------------------------------
	// Patch vertex layout
	// One float2 per vertex (local normalised XZ in [0,1]).
	// Patch origin + size are passed as per-instance data.
	// -----------------------------------------------------------------------
	struct TerrainVertex
	{
		float localX;
		float localZ;
	};

	// -----------------------------------------------------------------------
	// Init
	// -----------------------------------------------------------------------
	bool TerrainRenderer::Init(uint32_t patchDim, GpuDevice& gpuDevice)
	{
		B3D_ASSERT(patchDim >= 2 && Bitwise::IsPow2(patchDim));

		Close();
		mPatchDim = patchDim;

		BuildPatchMesh(patchDim, gpuDevice);

		// Instance buffer — enough for kMaxPatchesPerFrame TerrainPatch structs.
		// Each patch: float originX, float originZ, float patchSize, uint morphFlags
		// = 16 bytes per patch
		constexpr uint32_t kPatchInstanceSize = sizeof(TerrainPatch);
		static_assert(sizeof(TerrainPatch) == 16, "TerrainPatch layout changed");

		{
			GpuBufferCreateInformation ci = GpuBufferCreateInformation::CreateVertex(
				kPatchInstanceSize, kMaxPatchesPerFrame, GpuBufferFlag::StoreOnGPU);
			mInstanceBuffer = gpuDevice.CreateGpuBuffer(ci);
		}
		{
			GpuBufferCreateInformation ci = GpuBufferCreateInformation::CreateStagingWrite(
				kPatchInstanceSize * kMaxPatchesPerFrame);
			mInstanceStaging = gpuDevice.CreateGpuBuffer(ci);
		}

		// Vertex description: slot 0 = per-vertex (localXZ), slot 1 = per-instance (TerrainPatch)
		{
			Vector<VertexElement> elements = {
				VertexElement(VET_FLOAT2, VES_POSITION, 0, 0, 0, 0),      // localXZ
				VertexElement(VET_FLOAT1, VES_TEXCOORD, 0, 1, 1, 0),      // originX
				VertexElement(VET_FLOAT1, VES_TEXCOORD, 1, 1, 1, 4),      // originZ
				VertexElement(VET_FLOAT1, VES_TEXCOORD, 2, 1, 1, 8),      // patchSize
				VertexElement(VET_UINT1, VES_TEXCOORD, 3, 1, 1, 12)       // morphFlags
			};
			mVertexDescription = B3DMakeShared<VertexDescription>(elements);
		}

		// Grab material instances (compile is async, first use will wait).
		// NOTE: RendererMaterial::Get() must be called on the render thread. The render-thread
		// materials are not driven from the main-thread TerrainSystem in Fase 1; they are
		// initialised lazily by the render compositor in Fase 2. We leave the pointers null
		// so RenderGeometry/RenderDepth early-out until the compositor wires them up.
		mMaterial      = nullptr;
		mDepthMaterial = nullptr;

		mIsInitialised = true;

		B3D_LOG(Log, LogTerrain,
			"TerrainRenderer: patch={0}x{0}, verts={1}, indices={2}",
			patchDim, (patchDim + 1) * (patchDim + 1), mIndexCount);

		return true;
	}

	void TerrainRenderer::Close()
	{
		mVertexBuffer.reset();
		mIndexBuffer.reset();
		mInstanceBuffer.reset();
		mInstanceStaging.reset();
		mVertexDescription.reset();
		mPatchCount      = 0;
		mIsInitialised   = false;
	}

	// -----------------------------------------------------------------------
	// Patch mesh generation
	// One shared quad grid: (patchDim+1)*(patchDim+1) vertices, 2*patchDim*patchDim triangles.
	// Algorithm adapted from Dagor LodGrid generate_patch_indices() (MIT).
	// -----------------------------------------------------------------------
	void TerrainRenderer::BuildPatchMesh(uint32_t patchDim, GpuDevice& gpuDevice)
	{
		const uint32_t vertsPerSide  = patchDim + 1;
		const uint32_t totalVerts    = vertsPerSide * vertsPerSide;
		const uint32_t totalIndices  = patchDim * patchDim * 6; // 2 tris per quad

		// Generate normalised [0,1] XZ positions
		TArray<TerrainVertex> verts;
		verts.Resize(totalVerts);
		const float step = 1.0f / (float)patchDim;
		for (uint32_t z = 0; z < vertsPerSide; ++z)
			for (uint32_t x = 0; x < vertsPerSide; ++x)
				verts[z * vertsPerSide + x] = { x * step, z * step };

		// Generate indices (CCW winding, flipping on checker pattern for morph compatibility)
		TArray<uint32_t> indices;
		indices.Resize(totalIndices);
		uint32_t idx = 0;
		for (uint32_t z = 0; z < patchDim; ++z)
		{
			for (uint32_t x = 0; x < patchDim; ++x)
			{
				const uint32_t tl = z * vertsPerSide + x;
				const uint32_t tr = tl + 1;
				const uint32_t bl = tl + vertsPerSide;
				const uint32_t br = bl + 1;

				// Alternate diagonal direction like Dagor startFlipped
				const bool flipDiag = ((x + z) & 1) != 0;
				if (!flipDiag)
				{
					indices[idx++] = tl; indices[idx++] = bl; indices[idx++] = tr;
					indices[idx++] = tr; indices[idx++] = bl; indices[idx++] = br;
				}
				else
				{
					indices[idx++] = tl; indices[idx++] = bl; indices[idx++] = br;
					indices[idx++] = tl; indices[idx++] = br; indices[idx++] = tr;
				}
			}
		}
		mIndexCount = totalIndices;

		// Upload vertex buffer via a staging buffer + CopyBufferToBuffer
		{
			GpuBufferCreateInformation ci = GpuBufferCreateInformation::CreateVertex(
				sizeof(TerrainVertex), totalVerts, GpuBufferFlag::StoreOnGPU);
			mVertexBuffer = gpuDevice.CreateGpuBuffer(ci);

			GpuBufferCreateInformation stagingCI = GpuBufferCreateInformation::CreateStagingWrite(
				sizeof(TerrainVertex) * totalVerts);
			auto staging = gpuDevice.CreateGpuBuffer(stagingCI);

			auto scope = staging->Map(GpuMapOption::Write);
			if (scope)
				std::memcpy(scope.GetMappedMemory(), verts.Data(), sizeof(TerrainVertex) * totalVerts);
			scope.Unmap();

			// Note: the actual copy requires a command buffer. The caller (Init) does not have
			// one, so we record this as a pending upload. In practice the renderer's BeginFrame
			// performs the staging->GPU copy on the first frame. For simplicity we copy here
			// via the device's immediate context if available; otherwise we leave it to the
			// first frame. The Vulkan/D3D12 backends expose a transfer queue through the
			// command buffer, so we defer the copy to BeginFrame.
			mPendingVertexStaging = staging;
		}

		// Upload index buffer (same deferred pattern)
		{
			GpuBufferCreateInformation ci = GpuBufferCreateInformation::CreateIndex(
				IT_32BIT, totalIndices, GpuBufferFlag::StoreOnGPU);
			mIndexBuffer = gpuDevice.CreateGpuBuffer(ci);

			GpuBufferCreateInformation stagingCI = GpuBufferCreateInformation::CreateStagingWrite(
				sizeof(uint32_t) * totalIndices);
			auto staging = gpuDevice.CreateGpuBuffer(stagingCI);

			auto scope = staging->Map(GpuMapOption::Write);
			if (scope)
				std::memcpy(scope.GetMappedMemory(), indices.Data(), sizeof(uint32_t) * totalIndices);
			scope.Unmap();

			mPendingIndexStaging = staging;
		}
	}

	// -----------------------------------------------------------------------
	// Per-frame instance upload
	// -----------------------------------------------------------------------
	void TerrainRenderer::UploadInstances(const TerrainLodCullResult& cullResult,
		render::GpuCommandBuffer& commandBuffer)
	{
		const uint32_t patchCount = (uint32_t)Math::Min(
			(uint64_t)kMaxPatchesPerFrame, (uint64_t)cullResult.mPatches.Size());

		mPatchCount = patchCount;
		if (patchCount == 0) return;

		const uint32_t byteSize = patchCount * sizeof(TerrainPatch);

		// Write to staging
		auto scope = mInstanceStaging->Map(GpuMapOption::Write);
		if (scope)
			std::memcpy(scope.GetMappedMemory(), cullResult.mPatches.Data(), byteSize);
		scope.Unmap();

		// Copy staging -> GPU instance buffer
		commandBuffer.CopyBufferToBuffer(mInstanceStaging, mInstanceBuffer, 0, 0, byteSize);
	}

	// -----------------------------------------------------------------------
	// BeginFrame
	// -----------------------------------------------------------------------
	void TerrainRenderer::BeginFrame(
		const TerrainLodCullResult& cullResult,
		const TShared<Texture>& heightmapTex,
		const Matrix4& viewProjMat,
		const Vector3& cameraPos,
		float heightMin, float heightScale,
		Vector2 worldOffset, Vector2 worldSizeXZ,
		float morphStart,
		const TShared<Texture>& indirectionTex,
		const TShared<Texture>& atlasAlbedo,
		const TShared<Texture>& atlasNormal,
		const TShared<Texture>& atlasRoughAO,
		render::GpuCommandBuffer& commandBuffer)
	{
		// ---- Deferred staging uploads from Init() ----
		if (mPendingVertexStaging)
		{
			commandBuffer.CopyBufferToBuffer(mPendingVertexStaging, mVertexBuffer, 0, 0,
				sizeof(TerrainVertex) * (mPatchDim + 1) * (mPatchDim + 1));
			mPendingVertexStaging.reset();
		}
		if (mPendingIndexStaging)
		{
			commandBuffer.CopyBufferToBuffer(mPendingIndexStaging, mIndexBuffer, 0, 0,
				sizeof(uint32_t) * mIndexCount);
			mPendingIndexStaging.reset();
		}

		// ---- Upload instance data ----
		UploadInstances(cullResult, commandBuffer);

		// ---- Fill frame uniform buffers (render-thread transient allocator) ----
		{
			GpuBufferMappedScope scope = gTerrainFrameParams.AllocateTransient().Map();
			const Vector4 heightmapScaleOfs(1.0f / Math::Max(0.0001f, worldSizeXZ.X),
				1.0f / Math::Max(0.0001f, worldSizeXZ.Y), worldOffset.X, worldOffset.Y);
			const Vector4 heightScaleOffset(heightScale, heightMin, 0.0f, 0.0f);
			const float morphEnd = 1.0f;
			const float morphRange = Math::Max(0.001f, morphEnd - morphStart);
			const Vector4 morphParams(morphStart, morphEnd, 1.0f / morphRange, 0.0f);
			const Vector4 vtexParams(0.0f, 0.0f, 0.0f, 0.0f); // filled by VTex in Fase 3
			gTerrainFrameParams.gHeightmapScaleOfs.Set(scope, heightmapScaleOfs);
			gTerrainFrameParams.gHeightScaleOffset.Set(scope, heightScaleOffset);
			gTerrainFrameParams.gMorphParams.Set(scope, morphParams);
			gTerrainFrameParams.gVTexParams.Set(scope, vtexParams);
			gTerrainFrameParams.gCameraPos.Set(scope, cameraPos);

			if (mMaterial)
				mMaterial->SetFrameParams(scope, heightmapScaleOfs, heightScaleOffset, morphParams, vtexParams, cameraPos);
			if (mDepthMaterial)
				mDepthMaterial->SetFrameParams(scope, heightmapScaleOfs, heightScaleOffset, morphParams, vtexParams, cameraPos);
		}
		{
			GpuBufferMappedScope scope = gTerrainLightParams.AllocateTransient().Map();
			const Matrix4 worldViewProj = viewProjMat;
			const Vector4 sunDir(0.0f, -1.0f, 0.0f, 0.0f);
			const Vector4 sunColor(1.0f, 1.0f, 1.0f, 1.0f);
			const Vector4 ambientColor(0.2f, 0.2f, 0.25f, 1.0f);
			gTerrainLightParams.gWorldViewProj.Set(scope, worldViewProj);
			gTerrainLightParams.gSunDirection.Set(scope, sunDir);
			gTerrainLightParams.gSunColor.Set(scope, sunColor);
			gTerrainLightParams.gAmbientColor.Set(scope, ambientColor);

			if (mMaterial)
				mMaterial->SetLightParams(scope, worldViewProj, sunDir, sunColor, ambientColor);
			if (mDepthMaterial)
				mDepthMaterial->SetLightParams(scope, worldViewProj, sunDir, sunColor, ambientColor);
		}

		// ---- Set textures on the material ----
		if (mMaterial)
		{
			mMaterial->SetHeightmap(heightmapTex);
			mMaterial->SetIndirectionTexture(indirectionTex);
			mMaterial->SetAtlasTextures(atlasAlbedo, atlasNormal, atlasRoughAO);
		}
	}

	// -----------------------------------------------------------------------
	// Render helpers
	// -----------------------------------------------------------------------
	void TerrainRenderer::DrawPatches(render::GpuCommandBuffer& commandBuffer) const
	{
		if (mPatchCount == 0) return;

		commandBuffer.SetVertexDescription(mVertexDescription);
		commandBuffer.SetDrawOperation(DOT_TRIANGLE_LIST);

		TShared<GpuBuffer> vbs[2] = { mVertexBuffer, mInstanceBuffer };
		commandBuffer.SetVertexBuffers(0, vbs, 2);
		commandBuffer.SetIndexBuffer(mIndexBuffer);

		// DrawIndexed instanced — one call covers all patches
		commandBuffer.DrawIndexed(
			/*startIndex=*/   0,
			/*indexCount=*/   mIndexCount,
			/*vertexOffset=*/ 0,
			/*vertexCount=*/  (mPatchDim + 1) * (mPatchDim + 1),
			/*instanceCount=*/mPatchCount,
			/*firstInstance=*/0);
	}

	void TerrainRenderer::RenderGeometry(render::GpuCommandBuffer& commandBuffer)
	{
		if (!mIsInitialised || !mMaterial) return;

		commandBuffer.BeginLabel("Terrain Geometry");
		mMaterial->Bind(commandBuffer);
		DrawPatches(commandBuffer);
		commandBuffer.EndLabel();
	}

	void TerrainRenderer::RenderDepth(render::GpuCommandBuffer& commandBuffer)
	{
		if (!mIsInitialised || !mDepthMaterial) return;

		commandBuffer.BeginLabel("Terrain Depth");
		mDepthMaterial->Bind(commandBuffer);
		DrawPatches(commandBuffer);
		commandBuffer.EndLabel();
	}

	// -----------------------------------------------------------------------
	// Material implementations
	// -----------------------------------------------------------------------

		void TerrainMaterial::Initialize()
		{
			TShared<GpuParameterSet> ps = GetGpuParameterSet();
			if (!ps) return;

			ps->GetUniformBufferParameter("gTerrainFrameParams", mFrameParamsBuf);
			ps->GetUniformBufferParameter("gTerrainPatchParams", mPatchParamsBuf);
			ps->GetUniformBufferParameter("gTerrainLightParams", mLightParamsBuf);
			ps->GetSampledTextureParameter("gHeightmapTex",      mHeightmapParam);
			ps->GetSampledTextureParameter("gIndirectionTex",    mIndirectionParam);
			ps->GetSampledTextureParameter("gAtlasAlbedo",       mAtlasAlbedoParam);
			ps->GetSampledTextureParameter("gAtlasNormal",       mAtlasNormalParam);
			ps->GetSampledTextureParameter("gAtlasRoughAO",      mAtlasRoughAOParam);
		}

		void TerrainMaterial::SetFrameParams(const GpuBufferMappedScope& scope,
			Vector4 heightmapScaleOfs, Vector4 heightScaleOffset,
			Vector4 morphParams, Vector4 vtexParams, Vector3 cameraPos)
		{
			(void)heightmapScaleOfs; (void)heightScaleOffset; (void)morphParams;
			(void)vtexParams; (void)cameraPos;
			// The uniform buffer members are filled directly on the global definition
			// by the renderer (gTerrainFrameParams.g* Set). Here we only bind the
			// transient suballocation that was mapped into @p scope.
			mFrameParamsBuf.Set(scope.GetSuballocation());
		}

		void TerrainMaterial::SetPatchParams(const GpuBufferMappedScope& scope, Vector4 patchOriginSize)
		{
			(void)patchOriginSize;
			mPatchParamsBuf.Set(scope.GetSuballocation());
		}

		void TerrainMaterial::SetLightParams(const GpuBufferMappedScope& scope, Matrix4 worldViewProj,
			Vector4 sunDirection, Vector4 sunColor, Vector4 ambientColor)
		{
			(void)worldViewProj; (void)sunDirection; (void)sunColor; (void)ambientColor;
			mLightParamsBuf.Set(scope.GetSuballocation());
		}

		void TerrainMaterial::SetHeightmap(const TShared<Texture>& tex)
		{
			mHeightmapParam.Set(tex);
		}

		void TerrainMaterial::SetIndirectionTexture(const TShared<Texture>& tex)
		{
			mIndirectionParam.Set(tex);
		}

		void TerrainMaterial::SetAtlasTextures(
			const TShared<Texture>& albedo,
			const TShared<Texture>& normal,
			const TShared<Texture>& roughAO)
		{
			mAtlasAlbedoParam.Set(albedo);
			mAtlasNormalParam.Set(normal);
			mAtlasRoughAOParam.Set(roughAO);
		}

		// ---- TerrainDepthMaterial ----

		void TerrainDepthMaterial::Initialize()
		{
			TShared<GpuParameterSet> ps = GetGpuParameterSet();
			if (!ps) return;

			ps->GetUniformBufferParameter("gTerrainFrameParams", mFrameParamsBuf);
			ps->GetUniformBufferParameter("gTerrainPatchParams", mPatchParamsBuf);
			ps->GetUniformBufferParameter("gTerrainLightParams", mLightParamsBuf);
		}

		void TerrainDepthMaterial::SetFrameParams(const GpuBufferMappedScope& scope,
			Vector4 heightmapScaleOfs, Vector4 heightScaleOffset,
			Vector4 morphParams, Vector4 vtexParams, Vector3 cameraPos)
		{
			(void)heightmapScaleOfs; (void)heightScaleOffset; (void)morphParams;
			(void)vtexParams; (void)cameraPos;
			mFrameParamsBuf.Set(scope.GetSuballocation());
		}

		void TerrainDepthMaterial::SetPatchParams(const GpuBufferMappedScope& scope, Vector4 patchOriginSize)
		{
			(void)patchOriginSize;
			mPatchParamsBuf.Set(scope.GetSuballocation());
		}

		void TerrainDepthMaterial::SetLightParams(const GpuBufferMappedScope& scope, Matrix4 worldViewProj,
			Vector4 sunDirection, Vector4 sunColor, Vector4 ambientColor)
		{
			(void)worldViewProj; (void)sunDirection; (void)sunColor; (void)ambientColor;
			mLightParamsBuf.Set(scope.GetSuballocation());
		}
	} // namespace render
} // namespace b3d