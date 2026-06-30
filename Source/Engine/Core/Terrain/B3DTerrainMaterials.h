//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Renderer/B3DRendererMaterial.h"
#include "GpuBackend/B3DGpuParameter.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "GpuBackend/B3DVertexDescription.h"
#include "Math/B3DMatrix4.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DVector4.h"
#include "Math/B3DConvexVolume.h"
#include "Image/B3DTexture.h"
#include "Terrain/B3DTerrainUniformBuffers.h"
#include "Terrain/B3DTerrainLodGrid.h"

namespace b3d
{
	class HeightmapData;
	class Texture;
	class Camera;

	namespace render
	{
		/**
		 * Terrain geometry pass material — renders LOD-grid patches sampling the
		 * heightmap in the vertex stage and the virtual texture atlas in the pixel stage.
		 *
		 * Backed by TerrainHeightmap.bsl (geo-clipmap VS + virtual-texture PS).
		 */
		class B3D_EXPORT TerrainMaterial : public RendererMaterial<TerrainMaterial>
		{
			RMAT_DEF("Terrain/TerrainHeightmap.bsl");

		public:
			TerrainMaterial() = default;

			/** Called once after material is compiled; caches parameter handles. */
			void Initialize() override;

			/** Uploads per-frame terrain constants. Must be called before Bind(). */
			void SetFrameParams(const GpuBufferMappedScope& scope,
				Vector4 heightmapScaleOfs, Vector4 heightScaleOffset,
				Vector4 morphParams, Vector4 vtexParams, Vector3 cameraPos);

			/** Uploads per-patch constants for a single instanced draw call. */
			void SetPatchParams(const GpuBufferMappedScope& scope, Vector4 patchOriginSize);

			/** Uploads lighting constants. */
			void SetLightParams(const GpuBufferMappedScope& scope, Matrix4 worldViewProj,
				Vector4 sunDirection, Vector4 sunColor, Vector4 ambientColor);

			/** Binds the R16 heightmap texture. */
			void SetHeightmap(const TShared<Texture>& heightmapTex);

			/** Binds the virtual texture indirection texture (RG16U). */
			void SetIndirectionTexture(const TShared<Texture>& indirectionTex);

			/** Binds the virtual texture atlas layers. */
			void SetAtlasTextures(const TShared<Texture>& albedo,
				const TShared<Texture>& normal,
				const TShared<Texture>& roughAO);

		private:
			GpuParameterUniformBuffer  mFrameParamsBuf;
			GpuParameterUniformBuffer  mPatchParamsBuf;
			GpuParameterUniformBuffer  mLightParamsBuf;
			GpuParameterSampledTexture mHeightmapParam;
			GpuParameterSampledTexture mIndirectionParam;
			GpuParameterSampledTexture mAtlasAlbedoParam;
			GpuParameterSampledTexture mAtlasNormalParam;
			GpuParameterSampledTexture mAtlasRoughAOParam;
		};

		/**
		 * Terrain depth-only pass material — renders terrain patches into the depth buffer
		 * only (shadow map passes, depth prepass). No texturing.
		 *
		 * Backed by TerrainDepth.bsl.
		 */
		class B3D_EXPORT TerrainDepthMaterial : public RendererMaterial<TerrainDepthMaterial>
		{
			RMAT_DEF("Terrain/TerrainDepth.bsl");

		public:
			TerrainDepthMaterial() = default;

			void Initialize() override;

			void SetFrameParams(const GpuBufferMappedScope& scope,
				Vector4 heightmapScaleOfs, Vector4 heightScaleOffset,
				Vector4 morphParams, Vector4 vtexParams, Vector3 cameraPos);

			void SetPatchParams(const GpuBufferMappedScope& scope, Vector4 patchOriginSize);

			void SetLightParams(const GpuBufferMappedScope& scope, Matrix4 worldViewProj,
				Vector4 sunDirection, Vector4 sunColor, Vector4 ambientColor);

		private:
			GpuParameterUniformBuffer mFrameParamsBuf;
			GpuParameterUniformBuffer mPatchParamsBuf;
			GpuParameterUniformBuffer mLightParamsBuf;
		};
	} // namespace render
} // namespace b3d