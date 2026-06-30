//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DRenderBeastPrerequisites.h"
#include "Renderer/B3DRendererMaterial.h"
#include "Renderer/B3DGpuUniformBuffer.h"
#include "Components/B3DSkybox.h"

namespace b3d
{
	namespace render
	{
		/** @addtogroup RenderBeast
		 *  @{
		 */

		/** Uniform buffer layout for the ProceduralSkyParams cbuffer (mirrors SkyProceduralCommon.bslinc ProceduralSkyParams). */
		B3D_UNIFORM_BUFFER_BEGIN(ProceduralSkyUniformDefinition)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gSunDirection)
			B3D_UNIFORM_BUFFER_MEMBER(float, gRayleigh)
			B3D_UNIFORM_BUFFER_MEMBER(float, gTurbidity)
			B3D_UNIFORM_BUFFER_MEMBER(float, gMieCoefficient)
			B3D_UNIFORM_BUFFER_MEMBER(float, gLuminance)
			B3D_UNIFORM_BUFFER_MEMBER(float, gMieDirectionalG)
			B3D_UNIFORM_BUFFER_MEMBER(Vector3, gPad)
		B3D_UNIFORM_BUFFER_END

		extern ProceduralSkyUniformDefinition gProceduralSkyUniformDefinition;

		/** Uniform buffer layout for the SkyCubeDims cbuffer (cube face size/face index/mip/sample count). */
		B3D_UNIFORM_BUFFER_BEGIN(SkyCubeDimsUniformDefinition)
			B3D_UNIFORM_BUFFER_MEMBER(u32, gCubeFaceSize)
			B3D_UNIFORM_BUFFER_MEMBER(u32, gCubeFaceIndex)
			B3D_UNIFORM_BUFFER_MEMBER(u32, gMipWidth)
			B3D_UNIFORM_BUFFER_MEMBER(u32, gSampleCount)
		B3D_UNIFORM_BUFFER_END

		extern SkyCubeDimsUniformDefinition gSkyCubeDimsUniformDefinition;

		/** Selects which pass a SkyProceduralMaterial variation executes. Mirrors the SKY_PASS shader variation. */
		enum class SkyProceduralPass
		{
			/** Generates the HDR environment cubemap from the Preetham model. */
			Environment,
			/** Convolves the environment cubemap into a diffuse irradiance cubemap. */
			Irradiance,
			/** Importance-samples the environment cubemap into a prefiltered radiance cubemap. */
			Prefiltered
		};

		/**
		 * Compute material that evaluates the Preetham analytic daylight model and produces the environment,
		 * irradiance and prefiltered radiance cubemaps used by the IBL pipeline. Wraps SkyProcedural.bsl.
		 *
		 * Each pass is a separate variation of the same shader, dispatched once per cube face. The ENVIRONMENT pass
		 * writes to a RWTexture2DArray; the IRRADIANCE and PREFILTERED passes read the environment cube and write
		 * to their own RWTexture2DArray outputs.
		 */
		class SkyProceduralMaterial : public RendererMaterial<SkyProceduralMaterial>
		{
			RMAT_DEF_CUSTOMIZED("SkyProcedural.bsl")

			/** Helper method used for initializing variations of this material. */
			template <int passId>
			static const ShaderVariationParameters& GetVariation()
			{
				static ShaderVariationParameters variation = ShaderVariationParameters(
					{ ShaderVariationParameter("SKY_PASS", passId) });

				return variation;
			}

		public:
			SkyProceduralMaterial() = default;
			void Initialize() override;

			/**
			 * Dispatches the environment generation pass for a single cube face. Writes the Preetham sky color into
			 * each texel of the face of @p environmentCube identified by @p faceIndex.
			 */
			void ExecuteEnvironment(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
				u32 faceIndex, u32 faceSize, const ProceduralSkyParams& params);

			/**
			 * Dispatches the irradiance convolution pass for a single cube face. Reads from @p environmentCube and
			 * writes the diffuse irradiance into @p irradianceCube at the face identified by @p faceIndex.
			 */
			void ExecuteIrradiance(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
				const TShared<Texture>& irradianceCube, u32 faceIndex, u32 faceSize);

			/**
			 * Dispatches the prefiltered radiance pass for a single cube face. Importance-samples @p environmentCube
			 * using @p sampleDirections and writes the prefiltered result into @p prefilteredCube.
			 */
			void ExecutePrefiltered(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
				const TShared<Texture>& prefilteredCube, const TShared<GpuBuffer>& sampleDirections,
				u32 faceIndex, u32 faceSize, u32 sampleCount);

			/** Returns the material variation matching the requested pass. */
			static SkyProceduralMaterial* GetVariation(SkyProceduralPass pass);

		private:
			GpuParameterUniformBuffer mProceduralSkyUniformParameter;
			GpuParameterUniformBuffer mSkyCubeDimsUniformParameter;
			GpuParameterStorageTexture mEnvironmentCubeOutputParameter;
			GpuParameterSampledTexture mEnvironmentCubeInputParameter;
			GpuParameterStorageTexture mIrradianceCubeOutputParameter;
			GpuParameterStorageTexture mPrefilteredCubeOutputParameter;
			GpuParameterStorageBuffer mSampleDirectionsParameter;
		};

		/** @} */
	} // namespace render
} // namespace b3d
