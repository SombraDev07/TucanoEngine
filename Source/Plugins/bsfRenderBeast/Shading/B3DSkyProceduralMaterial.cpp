//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DRenderBeastPrerequisites.h"
#include "Shading/B3DSkyProceduralMaterial.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "GpuBackend/B3DSamplerState.h"
#include "Image/B3DTexture.h"
#include "Math/B3DMath.h"

namespace b3d
{
	namespace render
	{
		ProceduralSkyUniformDefinition gProceduralSkyUniformDefinition;
		SkyCubeDimsUniformDefinition gSkyCubeDimsUniformDefinition;

		namespace
		{
			constexpr u32 kThreadGroupX = 8;
			constexpr u32 kThreadGroupY = 8;

			TShared<SamplerState> GetLinearWrapSampler(GpuDevice& device)
			{
				SamplerStateCreateInformation desc;
				desc.MinFilter = FO_LINEAR;
				desc.MagFilter = FO_LINEAR;
				desc.MipFilter = FO_LINEAR;
				return device.FindOrCreateSamplerState(desc);
			}

			Vector2I GetDispatchSize(u32 faceSize)
			{
				Vector2I size;
				size.X = Math::DivideAndRoundUp(faceSize, kThreadGroupX);
				size.Y = Math::DivideAndRoundUp(faceSize, kThreadGroupY);
				return size;
			}
		}

		void SkyProceduralMaterial::InitDefinesInternal(ShaderDefines& defines)
		{
		}

		void SkyProceduralMaterial::Initialize()
		{
			mGpuParameterSet->GetUniformBufferParameter("ProceduralSkyParams", mProceduralSkyUniformParameter);
			mGpuParameterSet->GetUniformBufferParameter("SkyCubeDims", mSkyCubeDimsUniformParameter);

			mGpuParameterSet->TryGetStorageTextureParameter("gEnvironmentCube", mEnvironmentCubeOutputParameter);
			mGpuParameterSet->TryGetStorageTextureParameter("gIrradianceCube", mIrradianceCubeOutputParameter);
			mGpuParameterSet->TryGetStorageTextureParameter("gPrefilteredCube", mPrefilteredCubeOutputParameter);

			mGpuParameterSet->TryGetSampledTextureParameter("gEnvironmentCube", mEnvironmentCubeInputParameter);

			mGpuParameterSet->TryGetStorageBufferParameter("gSampleDirections", mSampleDirectionsParameter);
		}

		void SkyProceduralMaterial::ExecuteEnvironment(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
			u32 faceIndex, u32 faceSize, const ProceduralSkyParams& params)
		{
			B3D_PROFILE_RENDERER_MATERIAL

			GpuBufferMappedScope skyUniforms = gProceduralSkyUniformDefinition.AllocateTransient().Map();
			gProceduralSkyUniformDefinition.gSunDirection.Set(skyUniforms, Vector4(params.SunDirection, 0.0f));
			gProceduralSkyUniformDefinition.gRayleigh.Set(skyUniforms, params.Rayleigh);
			gProceduralSkyUniformDefinition.gTurbidity.Set(skyUniforms, params.Turbidity);
			gProceduralSkyUniformDefinition.gMieCoefficient.Set(skyUniforms, params.MieCoefficient);
			gProceduralSkyUniformDefinition.gLuminance.Set(skyUniforms, params.Luminance);
			gProceduralSkyUniformDefinition.gMieDirectionalG.Set(skyUniforms, params.MieDirectionalG);
			gProceduralSkyUniformDefinition.gPad.Set(skyUniforms, Vector3::kZero);
			mProceduralSkyUniformParameter.Set(skyUniforms);

			GpuBufferMappedScope dimsUniforms = gSkyCubeDimsUniformDefinition.AllocateTransient().Map();
			gSkyCubeDimsUniformDefinition.gCubeFaceSize.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gCubeFaceIndex.Set(dimsUniforms, faceIndex);
			gSkyCubeDimsUniformDefinition.gMipWidth.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gSampleCount.Set(dimsUniforms, 0u);
			mSkyCubeDimsUniformParameter.Set(dimsUniforms);

			mEnvironmentCubeOutputParameter.Set(environmentCube, TextureSurface(0, 1, 0, 0, true));

			Bind(commandBuffer);

			const Vector2I dispatchSize = GetDispatchSize(faceSize);
			commandBuffer.DispatchCompute(dispatchSize.X, dispatchSize.Y);
		}

		void SkyProceduralMaterial::ExecuteIrradiance(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
			const TShared<Texture>& irradianceCube, u32 faceIndex, u32 faceSize)
		{
			B3D_PROFILE_RENDERER_MATERIAL

			GpuBufferMappedScope dimsUniforms = gSkyCubeDimsUniformDefinition.AllocateTransient().Map();
			gSkyCubeDimsUniformDefinition.gCubeFaceSize.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gCubeFaceIndex.Set(dimsUniforms, faceIndex);
			gSkyCubeDimsUniformDefinition.gMipWidth.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gSampleCount.Set(dimsUniforms, 0u);
			mSkyCubeDimsUniformParameter.Set(dimsUniforms);

			mEnvironmentCubeInputParameter.Set(environmentCube);
			mIrradianceCubeOutputParameter.Set(irradianceCube, TextureSurface(0, 1, 0, 0, true));

			TShared<SamplerState> linearWrap = GetLinearWrapSampler(commandBuffer.GetGpuDevice());
			mGpuParameterSet->SetSamplerState("gLinearWrapSamp", linearWrap);

			Bind(commandBuffer);

			const Vector2I dispatchSize = GetDispatchSize(faceSize);
			commandBuffer.DispatchCompute(dispatchSize.X, dispatchSize.Y);
		}

		void SkyProceduralMaterial::ExecutePrefiltered(GpuCommandBuffer& commandBuffer, const TShared<Texture>& environmentCube,
			const TShared<Texture>& prefilteredCube, const TShared<GpuBuffer>& sampleDirections,
			u32 faceIndex, u32 faceSize, u32 sampleCount)
		{
			B3D_PROFILE_RENDERER_MATERIAL

			GpuBufferMappedScope dimsUniforms = gSkyCubeDimsUniformDefinition.AllocateTransient().Map();
			gSkyCubeDimsUniformDefinition.gCubeFaceSize.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gCubeFaceIndex.Set(dimsUniforms, faceIndex);
			gSkyCubeDimsUniformDefinition.gMipWidth.Set(dimsUniforms, faceSize);
			gSkyCubeDimsUniformDefinition.gSampleCount.Set(dimsUniforms, sampleCount);
			mSkyCubeDimsUniformParameter.Set(dimsUniforms);

			mEnvironmentCubeInputParameter.Set(environmentCube);
			mPrefilteredCubeOutputParameter.Set(prefilteredCube, TextureSurface(0, 1, 0, 0, true));
			mSampleDirectionsParameter.Set(sampleDirections);

			TShared<SamplerState> linearWrap = GetLinearWrapSampler(commandBuffer.GetGpuDevice());
			mGpuParameterSet->SetSamplerState("gLinearWrapSamp", linearWrap);

			Bind(commandBuffer);

			const Vector2I dispatchSize = GetDispatchSize(faceSize);
			commandBuffer.DispatchCompute(dispatchSize.X, dispatchSize.Y);
		}

		SkyProceduralMaterial* SkyProceduralMaterial::GetVariation(SkyProceduralPass pass)
		{
			switch (pass)
			{
			case SkyProceduralPass::Environment:
				return Get(GetVariation<0>());
			case SkyProceduralPass::Irradiance:
				return Get(GetVariation<1>());
			case SkyProceduralPass::Prefiltered:
			default:
				return Get(GetVariation<2>());
			}
		}
	} // namespace render
} // namespace b3d
