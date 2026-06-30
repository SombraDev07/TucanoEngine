//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//

#include "B3DRenderCompositor.h"
#include "B3DRendererView.h"
#include "B3DRenderBeast.h"
#include "B3DRenderBeastOptions.h"
#include "B3DRenderBeastIBLUtility.h"
#include "Shading/B3DSkyProceduralMaterial.h"
#include "Renderer/B3DRenderSettings.h"
#include "Renderer/B3DRendererUtility.h"
#include "Renderer/B3DRendererMaterial.h"
#include "Renderer/B3DGpuResourcePool.h"
#include "Renderer/B3DIBLUtility.h"
#include "Renderer/B3DRenderer.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "GpuBackend/B3DGpuProgram.h"
#include "GpuBackend/B3DGpuParameterSet.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "Components/B3DCamera.h"
#include "Components/B3DSkybox.h"
#include "Image/B3DTexture.h"
#include "Math/B3DMath.h"
#include "Mesh/B3DMesh.h"
#include "GpuBackend/B3DRenderTarget.h"
#include "GpuBackend/B3DRenderTexture.h"

namespace b3d
{
	namespace render
	{
		namespace
		{
			/** Cube face size used for the procedural environment cubemap. */
			constexpr u32 kSkyCubeSize = 512;

			/** Number of importance samples used by the prefiltered radiance pass. */
			constexpr u32 kPrefilterSampleCount = 1024;

			/** Hashes the procedural sky parameters to detect changes that require cubemap regeneration. */
			u64 HashProceduralParams(const ProceduralSkyParams& params)
			{
				u64 hash = 1469598103934665603ULL;
				auto mix = [&](const void* data, u32 size)
				{
					const auto* bytes = static_cast<const u8*>(data);
					for (u32 i = 0; i < size; ++i)
					{
						hash ^= bytes[i];
						hash *= 1099511628211ULL;
					}
				};
				mix(&params.SunDirection, sizeof(params.SunDirection));
				mix(&params.Rayleigh, sizeof(params.Rayleigh));
				mix(&params.Turbidity, sizeof(params.Turbidity));
				mix(&params.MieCoefficient, sizeof(params.MieCoefficient));
				mix(&params.MieDirectionalG, sizeof(params.MieDirectionalG));
				mix(&params.Luminance, sizeof(params.Luminance));
				return hash;
			}

			/** Creates a cubemap texture suitable for use as a compute UAV. */
			TShared<Texture> CreateSkyCubeTexture(GpuDevice& device, u32 size, PixelFormat format)
			{
				TextureCreateInformation tci;
				tci.Format = format;
				tci.Width = size;
				tci.Height = size;
				tci.Depth = 1;
				tci.MipMapCount = 0;
				tci.SampleCount = 0;
				tci.Usage = TextureUsageFlag::StoreOnGPU | TextureUsageFlag::AllowUnorderedAccessOnTheGPU;
				tci.Type = TEX_TYPE_CUBE_MAP;
				tci.UseHardwareSRGB = false;
				tci.ArraySliceCount = 1;
				return device.CreateTexture(tci);
			}

			/** Generates a Hammersley low-discrepancy sequence of @p sampleCount directions and uploads it to a
			 *  StructuredBuffer<float4> consumed by the PREFILTERED pass. Returns the buffer and fills @p data
			 *  with the CPU-side sample data to be uploaded via GpuBufferUtility::Write. */
			TShared<GpuBuffer> CreateSampleDirectionsBuffer(GpuDevice& device, u32 sampleCount, Vector<Vector4>& data)
			{
				data.reserve(sampleCount);

				auto radicalInverse = [](u32 bits)
				{
					bits = (bits << 16u) | (bits >> 16u);
					bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
					bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
					bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
					bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
					return float(bits) * 2.3283064365386963e-10f;
				};

				for (u32 i = 0; i < sampleCount; ++i)
				{
					const float u = float(i) / float(sampleCount);
					const float v = radicalInverse(i);

					const float phi = v * 2.0f * 3.14159265358979f;
					const float cosTheta = 1.0f - u;
					const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

					Vector4 dir;
					dir.X = sinTheta * std::cos(phi);
					dir.Y = sinTheta * std::sin(phi);
					dir.Z = cosTheta;
					dir.W = 0.0f;
					data.push_back(dir);
				}

				GpuBufferCreateInformation bci;
				bci.Type = GpuBufferType::StructuredStorage;
				bci.Flags = GpuBufferFlag::StoreOnGPU | GpuBufferFlag::AllowUnorderedAccessOnTheGPU;
				bci.StructuredStorage.Count = sampleCount;
				bci.StructuredStorage.ElementSize = sizeof(Vector4);

				return device.CreateGpuBuffer(bci);
			}
		}

		void RCNodeSkyProcedural::RegenerateCubemaps(GpuCommandBuffer& commandBuffer, const ProceduralSkyParams& params)
		{
			GpuDevice& device = commandBuffer.GetGpuDevice();

			if (mEnvironmentCube == nullptr)
			{
				mEnvironmentCube = CreateSkyCubeTexture(device, kSkyCubeSize, PF_RGBA32F);
				mIrradianceCube = CreateSkyCubeTexture(device, kSkyCubeSize, PF_RGBA32F);
				mFilteredRadianceCube = CreateSkyCubeTexture(device, kSkyCubeSize, PF_RGBA32F);

				Vector<Vector4> sampleData;
				mSampleDirections = CreateSampleDirectionsBuffer(device, kPrefilterSampleCount, sampleData);

				GpuWorkContext& gpuContext = GetRenderer()->GetGpuContext();
				GpuBufferUtility::Write(gpuContext, mSampleDirections, 0,
					sizeof(Vector4) * kPrefilterSampleCount, sampleData.data(), GpuBufferWriteFlag::Discard,
					commandBuffer.GetShared());
			}

			SkyProceduralMaterial* envMaterial = SkyProceduralMaterial::GetVariation(SkyProceduralPass::Environment);
			for (u32 face = 0; face < 6; ++face)
				envMaterial->ExecuteEnvironment(commandBuffer, mEnvironmentCube, face, kSkyCubeSize, params);

			SkyProceduralMaterial* irrMaterial = SkyProceduralMaterial::GetVariation(SkyProceduralPass::Irradiance);
			for (u32 face = 0; face < 6; ++face)
				irrMaterial->ExecuteIrradiance(commandBuffer, mEnvironmentCube, mIrradianceCube, face, kSkyCubeSize);

			SkyProceduralMaterial* preMaterial = SkyProceduralMaterial::GetVariation(SkyProceduralPass::Prefiltered);
			for (u32 face = 0; face < 6; ++face)
				preMaterial->ExecutePrefiltered(commandBuffer, mEnvironmentCube, mFilteredRadianceCube,
					mSampleDirections, face, kSkyCubeSize, kPrefilterSampleCount);

			mLastParamsHash = HashProceduralParams(params);
		}

		void RCNodeSkyProcedural::Render(const RenderCompositorNodeInputs& inputs)
		{
			Skybox* skybox = nullptr;
			if (inputs.View.GetRenderSettings().EnableSkybox)
				skybox = inputs.Scene.GetSkybox();

			GpuCommandBuffer& commandBuffer = *inputs.ActiveCommandBuffer;

			const bool useProcedural = skybox != nullptr && skybox->GetSkyMode() != SkyMode::Cubemap;

			if (!useProcedural)
			{
				// Fallback to cubemap skybox behavior when procedural mode is not enabled.
				TShared<Texture> radiance = skybox ? skybox->GetTexture() : nullptr;
				SkyboxMaterial* material;
				if (radiance != nullptr)
				{
					material = SkyboxMaterial::GetVariation(false);
					material->Bind(commandBuffer, inputs.View.GetPerViewBuffer(), radiance, Color::kWhite);
				}
				else
				{
					Color clearColor = inputs.View.GetProperties().Target.ClearColor.GetLinear();
					material = SkyboxMaterial::GetVariation(true);
					material->Bind(commandBuffer, inputs.View.GetPerViewBuffer(), nullptr, clearColor);
				}

				auto dependencies = GetDependencyDefinition().ResolveDependencies(inputs);
				RCNodeSceneColor* sceneColorNode = dependencies.Get<RCNodeSceneColor>();

				commandBuffer.BeginRenderPass(RenderPassCreateInformation(sceneColorNode->RenderTarget, material->GetGpuParameterSet(), RT_DEPTH_STENCIL, RT_COLOR0 | RT_DEPTH_STENCIL));

				Area2 area(0.0f, 0.0f, 1.0f, 1.0f);
				commandBuffer.SetViewport(area);

				TShared<Mesh> mesh = GetRendererUtility().GetSkyBoxMesh();
				GetRendererUtility().Draw(commandBuffer, mesh, mesh->GetProperties().SubMeshes[0]);

				commandBuffer.EndRenderPass();
				return;
			}

			const ProceduralSkyParams& params = skybox->GetProceduralSky();
			const u64 currentHash = HashProceduralParams(params);
			if (currentHash != mLastParamsHash)
				RegenerateCubemaps(commandBuffer, params);

			SkyboxMaterial* material = SkyboxMaterial::GetVariation(false);
			material->Bind(commandBuffer, inputs.View.GetPerViewBuffer(), mEnvironmentCube, Color::kWhite);

			auto dependencies = GetDependencyDefinition().ResolveDependencies(inputs);
			RCNodeSceneColor* sceneColorNode = dependencies.Get<RCNodeSceneColor>();

			commandBuffer.BeginRenderPass(RenderPassCreateInformation(sceneColorNode->RenderTarget, material->GetGpuParameterSet(), RT_DEPTH_STENCIL, RT_COLOR0 | RT_DEPTH_STENCIL));

			Area2 area(0.0f, 0.0f, 1.0f, 1.0f);
			commandBuffer.SetViewport(area);

			TShared<Mesh> mesh = GetRendererUtility().GetSkyBoxMesh();
			GetRendererUtility().Draw(commandBuffer, mesh, mesh->GetProperties().SubMeshes[0]);

			commandBuffer.EndRenderPass();
		}

		void RCNodeSkyProcedural::Clear()
		{
			mEnvironmentCube = nullptr;
			mIrradianceCube = nullptr;
			mFilteredRadianceCube = nullptr;
			mSampleDirections = nullptr;
			mLastParamsHash = 0;
		}

		RCNodeSkyProcedural::DependencyDefinition RCNodeSkyProcedural::GetDependencyDefinition()
		{
			static const DependencyDefinition kDependencyDefinition;
			return kDependencyDefinition;
		}
	} // namespace render
} // namespace b3d
