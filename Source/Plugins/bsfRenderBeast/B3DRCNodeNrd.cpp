//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//

#include "B3DRenderCompositor.h"
#include "B3DRenderBeast.h"
#include "B3DRenderBeastOptions.h"
#include "B3DRendererView.h"
#include "Components/B3DCamera.h"
#include "Renderer/B3DRenderSettings.h"
#include "Renderer/B3DGpuResourcePool.h"
#include "Renderer/B3DRendererUtility.h"

#if B3D_ENABLE_NRD

#include <NRD.h>
#include <NRDDescs.h>
#include <NRDSettings.h>

#include <NRI.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIWrapperVK.h>

#include "NRDIntegration.h"
#include "NRDIntegration.hpp"

#include "B3DVulkanGpuDevice.h"
#include "B3DVulkanGpuCommandBuffer.h"
#include "B3DVulkanTexture.h"
#include "B3DVulkanUtility.h"
#include "B3DVulkanGpuBackend.h"
#include "Shading/B3DPostProcessing.h"

#endif

namespace b3d
{
	namespace render
	{
#if B3D_ENABLE_NRD

		struct NrdIntegrationHolder
		{
			nrd::Integration integration;
			nrd::InstanceCreationDesc instanceDesc = {};
			nrd::DenoiserDesc denoiserDescs[1] = {};
		};

		static VkFormat B3DPixelFormatToVkFormat(PixelFormat format)
		{
			switch (format)
			{
			case PF_R8: return VK_FORMAT_R8_UNORM;
			case PF_RG8: return VK_FORMAT_R8G8_UNORM;
			case PF_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
			case PF_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
			case PF_R16F: return VK_FORMAT_R16_SFLOAT;
			case PF_RG16F: return VK_FORMAT_R16G16_SFLOAT;
			case PF_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case PF_R32F: return VK_FORMAT_R32_SFLOAT;
			case PF_RG32F: return VK_FORMAT_R32G32_SFLOAT;
			case PF_RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case PF_D32: return VK_FORMAT_D32_SFLOAT;
			case PF_D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
			default: return VK_FORMAT_UNDEFINED;
			}
		}

		static nrd::Resource GetNrdResource(const TShared<Texture>& tex, nri::AccessLayoutStage state)
		{
			nrd::Resource resource = {};
			if (!tex)
				return resource;

			VulkanTexture* vkTex = static_cast<VulkanTexture*>(tex.get());
			if (!vkTex || !vkTex->GetVulkanResource())
				return resource;

			resource.vk.image = vkTex->GetVulkanResource()->GetVulkanHandle();
			resource.vk.format = B3DPixelFormatToVkFormat(tex->GetProperties().Format);
			resource.state = state;
			resource.userArg = nullptr;
			return resource;
		}

		static void Matrix4ToFloat16(const Matrix4& m, float out[16])
		{
			out[0]  = m[0][0]; out[1]  = m[1][0]; out[2]  = m[2][0]; out[3]  = m[3][0];
			out[4]  = m[0][1]; out[5]  = m[1][1]; out[6]  = m[2][1]; out[7]  = m[3][1];
			out[8]  = m[0][2]; out[9]  = m[1][2]; out[10] = m[2][2]; out[11] = m[3][2];
			out[12] = m[0][3]; out[13] = m[1][3]; out[14] = m[2][3]; out[15] = m[3][3];
		}

#endif

		RCNodeNrd::~RCNodeNrd()
		{
#if B3D_ENABLE_NRD
			ReleaseContext();
#endif
		}

		RCNodeNrd::DependencyDefinition RCNodeNrd::GetDependencyDefinition()
		{
			return DependencyDefinition();
		}

		void RCNodeNrd::DeallocOutputs()
		{
			mPooledDiffuseOut = nullptr;
			mPooledSpecularOut = nullptr;
			mPooledNormalRoughness = nullptr;
			mPooledViewZ = nullptr;
			mPooledDiffuseIn = nullptr;
			mPooledSpecularIn = nullptr;
		}

		void RCNodeNrd::Clear()
		{
			DeallocOutputs();
			OutputDiffuse = nullptr;
			OutputSpecular = nullptr;
		}

#if B3D_ENABLE_NRD

		void RCNodeNrd::ReleaseContext()
		{
			if (mNrdIntegration)
			{
				NrdIntegrationHolder* holder = static_cast<NrdIntegrationHolder*>(mNrdIntegration);
				holder->integration.Destroy();
				delete holder;
				mNrdIntegration = nullptr;
			}
			mContextInitialized = false;
		}

		void RCNodeNrd::Render(const RenderCompositorNodeInputs& inputs)
		{
			const RenderSettings& settings = inputs.View.GetRenderSettings();

			if (!settings.EnableNrd)
				return;

			RCNodeDependencies deps = GetDependencyDefinition().ResolveDependencies(inputs);
			RCNodeSceneColor* sceneColorNode = deps.Get<RCNodeSceneColor>();
			RCNodeSceneDepth* sceneDepthNode = deps.Get<RCNodeSceneDepth>();
			RCNodeBasePass* basePassNode = deps.Get<RCNodeBasePass>();

			if (!sceneColorNode || !sceneDepthNode || !basePassNode)
				return;

			const RendererViewProperties& viewProperties = inputs.View.GetProperties();
			const u32 renderWidth = viewProperties.Target.ViewRect.Width;
			const u32 renderHeight = viewProperties.Target.ViewRect.Height;

			VulkanGpuCommandBuffer* vkCmdBuffer = static_cast<VulkanGpuCommandBuffer*>(inputs.ActiveCommandBuffer.get());
			VulkanGpuDevice* vkDevice = static_cast<VulkanGpuDevice*>(&vkCmdBuffer->GetGpuDevice());

			VulkanGpuBackend& vkBackend = static_cast<VulkanGpuBackend&>(GpuBackend::Instance());
			VkInstance vkInstance = vkBackend.GetVkInstance();

			if (mContextInitialized && (mResourceSize.X != renderWidth || mResourceSize.Y != renderHeight))
				ReleaseContext();

			if (!mContextInitialized)
			{
				NrdIntegrationHolder* holder = new NrdIntegrationHolder();

				holder->denoiserDescs[0].identifier = 0;
				holder->denoiserDescs[0].denoiser = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;

				holder->instanceDesc.denoisers = holder->denoiserDescs;
				holder->instanceDesc.denoisersNum = 1;
				holder->instanceDesc.allocationCallbacks = {};

				nrd::IntegrationCreationDesc integrationDesc = {};
				integrationDesc.resourceWidth = static_cast<uint16_t>(renderWidth);
				integrationDesc.resourceHeight = static_cast<uint16_t>(renderHeight);
				integrationDesc.queuedFrameNum = 3;
				integrationDesc.autoWaitForIdle = true;
				integrationDesc.enableWholeLifetimeDescriptorCaching = false;
				strncpy(integrationDesc.name, "B3DNRD", sizeof(integrationDesc.name));

				nri::QueueFamilyVKDesc queueFamilyDesc = {};
				queueFamilyDesc.queueType = nri::QueueType::GRAPHICS;
				queueFamilyDesc.familyIndex = vkDevice->GetQueueFamily(GpuQueueType::GQT_GRAPHICS);
				queueFamilyDesc.queueNum = 1;

				nri::DeviceCreationVKDesc deviceCreationDesc = {};
				deviceCreationDesc.vkInstance = vkInstance;
				deviceCreationDesc.vkDevice = vkDevice->GetLogical();
				deviceCreationDesc.vkPhysicalDevice = vkDevice->GetPhysical();
				deviceCreationDesc.queueFamilies = &queueFamilyDesc;
				deviceCreationDesc.queueFamilyNum = 1;
				deviceCreationDesc.minorVersion = 2;
				deviceCreationDesc.enableNRIValidation = false;

				nrd::Result result = holder->integration.RecreateVK(integrationDesc, holder->instanceDesc, deviceCreationDesc);
				if (result != nrd::Result::SUCCESS)
				{
					B3D_LOG(Error, LogRenderer, "NRD: RecreateVK failed (code {0}).", static_cast<int>(result));
					delete holder;
					return;
				}

				mNrdIntegration = holder;
				mContextInitialized = true;
				mResourceSize = Vector2I(renderWidth, renderHeight);
				mFirstDispatch = true;
				mFrameIndex = 0;
			}

			NrdIntegrationHolder* holder = static_cast<NrdIntegrationHolder*>(mNrdIntegration);
			nrd::Integration& nrd = holder->integration;

			nrd.NewFrame();

			nrd::CommonSettings commonSettings = {};
			commonSettings.resourceSize[0] = static_cast<uint16_t>(renderWidth);
			commonSettings.resourceSize[1] = static_cast<uint16_t>(renderHeight);
			commonSettings.resourceSizePrev[0] = static_cast<uint16_t>(renderWidth);
			commonSettings.resourceSizePrev[1] = static_cast<uint16_t>(renderHeight);
			commonSettings.rectSize[0] = static_cast<uint16_t>(renderWidth);
			commonSettings.rectSize[1] = static_cast<uint16_t>(renderHeight);
			commonSettings.rectSizePrev[0] = static_cast<uint16_t>(renderWidth);
			commonSettings.rectSizePrev[1] = static_cast<uint16_t>(renderHeight);
			commonSettings.frameIndex = mFrameIndex;
			commonSettings.timeDeltaBetweenFrames = inputs.FrameInfo.Timings.TimeDelta * 1000.0f;
			commonSettings.denoisingRange = 50000.0f;
			commonSettings.viewZScale = 1.0f;
			commonSettings.isMotionVectorInWorldSpace = false;
			commonSettings.motionVectorScale[0] = static_cast<float>(renderWidth);
			commonSettings.motionVectorScale[1] = static_cast<float>(renderHeight);
			commonSettings.motionVectorScale[2] = 0.0f;
			commonSettings.cameraJitter[0] = viewProperties.TemporalJitter.X;
			commonSettings.cameraJitter[1] = viewProperties.TemporalJitter.Y;
			commonSettings.cameraJitterPrev[0] = viewProperties.TemporalJitter.X;
			commonSettings.cameraJitterPrev[1] = viewProperties.TemporalJitter.Y;
			commonSettings.accumulationMode = mFirstDispatch ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;

			const Camera* camera = inputs.View.GetSceneCamera();
			if (camera)
			{
				Matrix4 viewMatrix = camera->GetViewMatrix();
				Matrix4 projMatrix = camera->GetProjectionMatrix();
				Matrix4 viewProj = projMatrix * viewMatrix;

				Matrix4ToFloat16(viewMatrix, commonSettings.worldToViewMatrix);
				Matrix4ToFloat16(projMatrix, commonSettings.viewToClipMatrix);
				Matrix4ToFloat16(viewProperties.PrevViewProjTransform, commonSettings.viewToClipMatrixPrev);
			}

			nrd.SetCommonSettings(commonSettings);

			nrd::ReblurSettings reblurSettings = {};
			reblurSettings.maxAccumulatedFrameNum = Math::Min(settings.NrdMaxAccumulatedFrames, 63u);
			reblurSettings.maxFastAccumulatedFrameNum = 6;
			reblurSettings.hitDistanceParameters.A = 3.0f;
			reblurSettings.hitDistanceParameters.B = 0.1f;
			reblurSettings.hitDistanceParameters.C = 20.0f;
			reblurSettings.diffusePrepassBlurRadius = 30.0f;
			reblurSettings.specularPrepassBlurRadius = 50.0f;
			reblurSettings.historyFixBasePixelStride = 14;

			nrd.SetDenoiserSettings(0, &reblurSettings);

			auto& gpuResourcePool = GpuResourcePool::Instance();
			const PixelFormat hdrFormat = PF_RGBA16F;
			const PixelFormat normalRoughnessFormat = PF_RGBA8;
			const PixelFormat viewZFormat = PF_R32F;

			mPooledNormalRoughness = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				normalRoughnessFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));
			mPooledViewZ = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				viewZFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));
			mPooledDiffuseIn = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				hdrFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));
			mPooledSpecularIn = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				hdrFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));
			mPooledDiffuseOut = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				hdrFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));
			mPooledSpecularOut = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				hdrFormat, renderWidth, renderHeight, TextureUsageFlag::RenderTarget, 1, false));

			OutputDiffuse = mPooledDiffuseOut->Texture;
			OutputSpecular = mPooledSpecularOut->Texture;

			GpuCommandBuffer& commandBuffer = *inputs.ActiveCommandBuffer;

			NrdPrepareNormalRoughnessMaterial* normalRoughMat = NrdPrepareNormalRoughnessMaterial::Get();
			normalRoughMat->Prepare(basePassNode->NormalTex->Texture, basePassNode->RoughMetalTex->Texture);
			normalRoughMat->Execute(commandBuffer, mPooledNormalRoughness->RenderTexture);

			NrdPrepareViewZMaterial* viewZMat = NrdPrepareViewZMaterial::Get();
			viewZMat->Prepare(sceneDepthNode->DepthTex->Texture);
			viewZMat->Execute(commandBuffer, mPooledViewZ->RenderTexture);

			BlitInformation blitInfo = BlitInformation::BlitColor(sceneColorNode->SceneColorTex->Texture, mPooledDiffuseIn->RenderTexture);
			GetRendererUtility().Blit(commandBuffer, blitInfo);

			RenderPassCreateInformation clearInfo(mPooledSpecularIn->RenderTexture);
			clearInfo.ClearMask = RT_COLOR;
			commandBuffer.BeginRenderPass(clearInfo);
			commandBuffer.EndRenderPass();

			nrd::ResourceSnapshot resourceSnapshot = {};
			resourceSnapshot.restoreInitialState = true;

			nri::AccessLayoutStage readState = {};
			readState.access = nri::AccessBits::SHADER_RESOURCE;
			readState.layout = nri::TextureLayout::SHADER_RESOURCE;
			readState.stages = nri::StageBits::COMPUTE_SHADER;

			nri::AccessLayoutStage writeState = {};
			writeState.access = nri::AccessBits::SHADER_RESOURCE_STORAGE;
			writeState.layout = nri::TextureLayout::SHADER_RESOURCE_STORAGE;
			writeState.stages = nri::StageBits::COMPUTE_SHADER;

			if (basePassNode->VelocityTex)
				resourceSnapshot.SetResource(nrd::ResourceType::IN_MV, GetNrdResource(basePassNode->VelocityTex->Texture, readState));
			resourceSnapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, GetNrdResource(mPooledNormalRoughness->Texture, readState));
			resourceSnapshot.SetResource(nrd::ResourceType::IN_VIEWZ, GetNrdResource(mPooledViewZ->Texture, readState));

			resourceSnapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, GetNrdResource(mPooledDiffuseIn->Texture, readState));
			resourceSnapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, GetNrdResource(mPooledSpecularIn->Texture, readState));

			resourceSnapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, GetNrdResource(mPooledDiffuseOut->Texture, writeState));
			resourceSnapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, GetNrdResource(mPooledSpecularOut->Texture, writeState));

			const nrd::Identifier denoisers[] = { 0 };
			nri::CommandBufferVKDesc cmdBufferDesc = {};
			cmdBufferDesc.vkCommandBuffer = vkCmdBuffer->GetVulkanHandle();
			cmdBufferDesc.queueType = nri::QueueType::GRAPHICS;

			nrd.DenoiseVK(denoisers, 1, cmdBufferDesc, resourceSnapshot);

			mFirstDispatch = false;
			mFrameIndex++;
		}

#else

		void RCNodeNrd::ReleaseContext()
		{
		}

		void RCNodeNrd::Render(const RenderCompositorNodeInputs& inputs)
		{
		}

#endif
	}
}
