//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//

#include "B3DRenderCompositor.h"
#include "B3DRendererView.h"
#include "Renderer/B3DRenderSettings.h"
#include "GpuBackend/B3DGpuDevice.h"
#include "GpuBackend/B3DGpuCommandBuffer.h"
#include "B3DRenderBeast.h"
#include "B3DRenderBeastOptions.h"
#include "Renderer/B3DGpuResourcePool.h"
#include "Components/B3DCamera.h"

#if B3D_ENABLE_FSR3

#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

#include "B3DVulkanGpuDevice.h"
#include "B3DVulkanGpuCommandBuffer.h"
#include "B3DVulkanTexture.h"
#include "B3DVulkanUtility.h"

#endif

namespace b3d
{
	namespace render
	{
		RCNodeFsr3::~RCNodeFsr3()
		{
#if B3D_ENABLE_FSR3
			ReleaseContext();
			if (mScratchBuffer)
			{
				free(mScratchBuffer);
				mScratchBuffer = nullptr;
				mScratchBufferSize = 0;
			}
#endif
		}

		RCNodeFsr3::DependencyDefinition RCNodeFsr3::GetDependencyDefinition()
		{
			return DependencyDefinition();
		}

		void RCNodeFsr3::DeallocOutputs()
		{
			if (mPooledOutput != nullptr)
				mPooledOutput = nullptr;
		}

		void RCNodeFsr3::Clear()
		{
			DeallocOutputs();
			Output = nullptr;
		}

#if B3D_ENABLE_FSR3

		void RCNodeFsr3::ReleaseContext()
		{
			if (mContextInitialized && mFsr3Context)
			{
				ffxFsr3ContextDestroy(static_cast<FfxFsr3Context*>(mFsr3Context));
				mContextInitialized = false;
			}
			if (mFsr3Context)
			{
				free(mFsr3Context);
				mFsr3Context = nullptr;
			}
		}

		static VkFormat GetVkFormat(PixelFormat format, bool sRGB)
		{
			switch (format)
			{
			case PF_R8: return sRGB ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
			case PF_RG8: return sRGB ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;
			case PF_RGB8: return sRGB ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;
			case PF_BGR8: return sRGB ? VK_FORMAT_B8G8R8_SRGB : VK_FORMAT_B8G8R8_UNORM;
			case PF_RGBA8: return sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
			case PF_BGRA8: return sRGB ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
			case PF_R16F: return VK_FORMAT_R16_SFLOAT;
			case PF_RG16F: return VK_FORMAT_R16G16_SFLOAT;
			case PF_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
			case PF_R32F: return VK_FORMAT_R32_SFLOAT;
			case PF_RG32F: return VK_FORMAT_R32G32_SFLOAT;
			case PF_RGB32F: return VK_FORMAT_R32G32B32_SFLOAT;
			case PF_RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
			case PF_D32_S8X24: return VK_FORMAT_D32_SFLOAT_S8_UINT;
			case PF_D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
			case PF_D32: return VK_FORMAT_D32_SFLOAT;
			case PF_D16: return VK_FORMAT_D16_UNORM;
			default: return VK_FORMAT_UNDEFINED;
			}
		}

		static FfxResource GetFfxResource(const TShared<Texture>& tex, const wchar_t* name, FfxResourceStates state, FfxResourceUsage usage)
		{
			if (!tex)
				return {};

			VulkanTexture* vkTex = static_cast<VulkanTexture*>(tex.get());
			if (!vkTex || !vkTex->GetVulkanResource())
				return {};

			VkImage vkImage = vkTex->GetVulkanResource()->GetVulkanHandle();
			const auto& props = tex->GetProperties();

			FfxResourceDescription desc = {};
			desc.type = FFX_RESOURCE_TYPE_TEXTURE2D;
			desc.format = ffxGetSurfaceFormatVK(GetVkFormat(props.Format, props.UseHardwareSRGB));
			desc.width = props.Width;
			desc.height = props.Height;
			desc.depth = props.Depth;
			desc.mipCount = props.MipMapCount;
			desc.flags = FFX_RESOURCE_FLAGS_NONE;
			desc.usage = usage;

			return ffxGetResourceVK(vkImage, desc, name, state);
		}

		void RCNodeFsr3::Render(const RenderCompositorNodeInputs& inputs)
		{
			const RenderSettings& settings = inputs.View.GetRenderSettings();

			if (!settings.EnableFsr3)
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
			const u32 displayWidth = Math::Max(1U, static_cast<u32>(Math::RoundToI32(viewProperties.Target.NrmViewRect.Width * viewProperties.Target.TargetWidth)));
			const u32 displayHeight = Math::Max(1U, static_cast<u32>(Math::RoundToI32(viewProperties.Target.NrmViewRect.Height * viewProperties.Target.TargetHeight)));

			VulkanGpuCommandBuffer* vkCmdBuffer = static_cast<VulkanGpuCommandBuffer*>(inputs.ActiveCommandBuffer.get());
			VulkanGpuDevice* vkDevice = static_cast<VulkanGpuDevice*>(&vkCmdBuffer->GetGpuDevice());

			if (mContextInitialized && (mDisplaySize.X != displayWidth || mDisplaySize.Y != displayHeight ||
				mRenderSize.X != renderWidth || mRenderSize.Y != renderHeight))
			{
				ReleaseContext();
			}

			if (!mContextInitialized)
			{
				const size_t maxContexts = FFX_FSR3_CONTEXT_COUNT;
				const size_t scratchSize = ffxGetScratchMemorySizeVK(vkDevice->GetPhysical(), maxContexts);

				if (mScratchBufferSize < scratchSize)
				{
					if (mScratchBuffer)
						free(mScratchBuffer);
					mScratchBuffer = malloc(scratchSize);
					mScratchBufferSize = scratchSize;
				}

				VkDeviceContext deviceContext = {};
				deviceContext.vkDevice = vkDevice->GetLogical();
				deviceContext.vkPhysicalDevice = vkDevice->GetPhysical();
				deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;

				FfxDevice ffxDevice = ffxGetDeviceVK(&deviceContext);

				FfxInterface ffxInterface = {};
				const FfxErrorCode ifaceErr = ffxGetInterfaceVK(&ffxInterface, ffxDevice, mScratchBuffer, mScratchBufferSize, maxContexts);
				if (ifaceErr != FFX_OK)
				{
					B3D_LOG(Error, LogRenderer, "FSR3: ffxGetInterfaceVK failed (code {0}).", static_cast<int>(ifaceErr));
					return;
				}

				if (!mFsr3Context)
					mFsr3Context = malloc(sizeof(FfxFsr3Context));

				FfxFsr3ContextDescription contextDesc = {};
				contextDesc.flags = FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3_ENABLE_AUTO_EXPOSURE |
					FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_UPSCALING_ONLY;
				if (settings.EnableFsr3FrameGeneration)
					contextDesc.flags |= FFX_FSR3_ENABLE_INTERPOLATION_ONLY;

				contextDesc.maxRenderSize.width = renderWidth;
				contextDesc.maxRenderSize.height = renderHeight;
				contextDesc.maxUpscaleSize.width = displayWidth;
				contextDesc.maxUpscaleSize.height = displayHeight;
				contextDesc.displaySize.width = displayWidth;
				contextDesc.displaySize.height = displayHeight;
				contextDesc.backendInterfaceSharedResources = ffxInterface;
				contextDesc.backendInterfaceUpscaling = ffxInterface;
				contextDesc.backendInterfaceFrameInterpolation = ffxInterface;
				contextDesc.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;

				const FfxErrorCode err = ffxFsr3ContextCreate(static_cast<FfxFsr3Context*>(mFsr3Context), &contextDesc);
				if (err != FFX_OK)
				{
					B3D_LOG(Error, LogRenderer, "FSR3: ffxFsr3ContextCreate failed (code {0}).", static_cast<int>(err));
					ReleaseContext();
					return;
				}

				mContextInitialized = true;
				mDisplaySize = Vector2I(displayWidth, displayHeight);
				mRenderSize = Vector2I(renderWidth, renderHeight);
				mFirstDispatch = true;
				mFrameIndex = 0;
			}

			auto& gpuResourcePool = GpuResourcePool::Instance();
			mPooledOutput = gpuResourcePool.Get(PooledRenderTextureCreateInformation::Create2D(
				sceneColorNode->SceneColorTex->Texture->GetProperties().Format, displayWidth, displayHeight,
				TextureUsageFlag::RenderTarget, 1, false));
			Output = mPooledOutput->Texture;

			FfxFsr3DispatchUpscaleDescription dispatchDesc = {};
			dispatchDesc.commandList = ffxGetCommandListVK(vkCmdBuffer->GetVulkanHandle());

			dispatchDesc.color = GetFfxResource(sceneColorNode->SceneColorTex->Texture, L"SceneColor",
				FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY);
			dispatchDesc.depth = GetFfxResource(sceneDepthNode->DepthTex->Texture, L"SceneDepth",
				FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY);
			if (basePassNode->VelocityTex)
			{
				dispatchDesc.motionVectors = GetFfxResource(basePassNode->VelocityTex->Texture, L"MotionVectors",
					FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY);
			}
			dispatchDesc.upscaleOutput = GetFfxResource(Output, L"FSR3Output",
				FFX_RESOURCE_STATE_UNORDERED_ACCESS, FFX_RESOURCE_USAGE_UAV);

			const Camera* camera = inputs.View.GetSceneCamera();
			bool cameraCut = mFirstDispatch;
			if (camera)
			{
				dispatchDesc.jitterOffset.x = viewProperties.TemporalJitter.X;
				dispatchDesc.jitterOffset.y = viewProperties.TemporalJitter.Y;

				dispatchDesc.motionVectorScale.x = static_cast<float>(renderWidth);
				dispatchDesc.motionVectorScale.y = static_cast<float>(renderHeight);

				const Vector3& origin = viewProperties.ViewOrigin;
				const float dx = origin.X - mPrevViewOrigin[0];
				const float dy = origin.Y - mPrevViewOrigin[1];
				const float dz = origin.Z - mPrevViewOrigin[2];
				if (dx * dx + dy * dy + dz * dz > 1.0f)
					cameraCut = true;

				mPrevViewOrigin[0] = origin.X;
				mPrevViewOrigin[1] = origin.Y;
				mPrevViewOrigin[2] = origin.Z;
				dispatchDesc.reset = cameraCut;

				dispatchDesc.enableSharpening = (settings.FsrSharpness > 0.0f);
				dispatchDesc.sharpness = Math::Clamp(settings.FsrSharpness, 0.0f, 1.0f);

				const Matrix4& proj = camera->GetProjectionMatrix();
				dispatchDesc.cameraFovAngleVertical = Math::Atan(1.0f / proj[1][1]).GetValueInRadians() * 2.0f;

				dispatchDesc.cameraNear = camera->GetNearClipDistance();
				dispatchDesc.cameraFar = camera->GetFarClipDistance();
			}
			else
			{
				dispatchDesc.jitterOffset.x = 0.0f;
				dispatchDesc.jitterOffset.y = 0.0f;
				dispatchDesc.motionVectorScale.x = static_cast<float>(renderWidth);
				dispatchDesc.motionVectorScale.y = static_cast<float>(renderHeight);
				dispatchDesc.reset = cameraCut;
				dispatchDesc.enableSharpening = (settings.FsrSharpness > 0.0f);
				dispatchDesc.sharpness = Math::Clamp(settings.FsrSharpness, 0.0f, 1.0f);
			}

			mFirstDispatch = false;

			dispatchDesc.renderSize.width = renderWidth;
			dispatchDesc.renderSize.height = renderHeight;
			dispatchDesc.upscaleSize.width = displayWidth;
			dispatchDesc.upscaleSize.height = displayHeight;

			dispatchDesc.frameTimeDelta = inputs.FrameInfo.Timings.TimeDelta * 1000.0f;
			dispatchDesc.preExposure = 1.0f;
			dispatchDesc.viewSpaceToMetersFactor = 1.0f;
			dispatchDesc.frameID = mFrameIndex++;
			dispatchDesc.flags = 0;

			const FfxErrorCode dispatchErr = ffxFsr3ContextDispatchUpscale(static_cast<FfxFsr3Context*>(mFsr3Context), &dispatchDesc);
			if (dispatchErr != FFX_OK)
			{
				B3D_LOG(Error, LogRenderer, "FSR3: ffxFsr3ContextDispatchUpscale failed (code {0}).", static_cast<int>(dispatchErr));
			}
		}

#else

		void RCNodeFsr3::ReleaseContext()
		{
		}

		void RCNodeFsr3::Render(const RenderCompositorNodeInputs& inputs)
		{
		}

#endif
	}
}
