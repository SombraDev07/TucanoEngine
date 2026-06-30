//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Scene/B3DComponent.h"
#include "CoreObject/B3DCoreObject.h"
#include "Reflection/B3DIReflectable.h"
#include "Script/B3DIScriptExportable.h"
#include "Math/B3DVector3.h"

namespace b3d
{
	namespace render
	{
		class RendererTask;
	}

	/** @addtogroup Rendering-Internal
	 *  @{
	 */

	/** Selects how the sky background is generated for a view. */
	enum class B3D_SCRIPT_EXPORT(DocumentationGroup(Rendering)) SkyMode
	{
		/** Sky radiance is sampled from a cubemap texture provided via SetTexture(). */
		Cubemap,
		/** Sky radiance is generated procedurally from the Preetham analytic daylight model. */
		Preetham
	};

	/** Parameters for the procedural (Preetham) sky model. */
	struct B3D_EXPORT B3D_SCRIPT_EXPORT(DocumentationGroup(Rendering)) ProceduralSkyParams : IReflectable, IScriptExportable
	{
		struct SyncPacket;

		B3D_SCRIPT_EXPORT()
		ProceduralSkyParams() = default;

		/** Normalized direction pointing toward the sun, in world space. */
		B3D_SCRIPT_EXPORT()
		Vector3 SunDirection = Vector3(0.0f, 1.0f, 0.0f);

		/** Rayleigh scattering coefficient. Higher values yield a bluer sky. Typical range [0, 30]. */
		B3D_SCRIPT_EXPORT(UIValueRange([0, 30]), UI(AsSlider))
		float Rayleigh = 1.0f;

		/** Atmospheric turbidity (aerosol density). 1 = clear sky, 10 = hazy. Range [1, 10]. */
		B3D_SCRIPT_EXPORT(UIValueRange([1, 10]), UI(AsSlider))
		float Turbidity = 2.0f;

		/** Mie scattering coefficient. Controls forward scattering from aerosols. Range [0, 0.1]. */
		B3D_SCRIPT_EXPORT(UIValueRange([0, 0.1]), UI(AsSlider))
		float MieCoefficient = 0.005f;

		/** Mie phase function anisotropy (Henyey-Greenstein g). Range [-1, 1]. */
		B3D_SCRIPT_EXPORT(UIValueRange([-1, 1]), UI(AsSlider))
		float MieDirectionalG = 0.8f;

		/** Luminance scale applied to the final sky color. Lower values produce a darker sky. */
		B3D_SCRIPT_EXPORT(UIValueRange([0.001, 10]), UI(AsSlider))
		float Luminance = 1.0f;

		/************************************************************************/
		/* 								RTTI		                     		*/
		/************************************************************************/
	public:
		friend class ProceduralSkyParamsRTTI;
		static RTTIType* GetRttiStatic();
		RTTIType* GetRtti() const override;
	};

	/** Templated base class for both render and main thread implementations of a skybox. */
	template <bool IsRenderProxy>
	class B3D_EXPORT TSkybox : public CoreVariantType<CoreObject, IsRenderProxy>
	{
	public:
		using TextureType = CoreVariantHandleType<Texture, IsRenderProxy>;
		using Super = CoreVariantType<CoreObject, IsRenderProxy>;

		TSkybox() = default;
		virtual ~TSkybox() = default;

		/**
		 * Brightness multiplier that will be applied to skybox values before they're being used. Allows you to make the
		 * skybox more or less bright. Equal to one by default.
		 */
		B3D_SCRIPT_EXPORT(ExportName(Brightness), Property(Setter))
		void SetBrightness(float brightness)
		{
			mBrightness = brightness;
			MarkRenderProxyDataDirty();
		}

		/** @copydoc SetBrightness */
		B3D_SCRIPT_EXPORT(ExportName(Brightness), Property(Getter))
		float GetBrightness() const { return mBrightness; }

		/** Selects how the sky background is generated. When set to Preetham the cubemap texture is ignored. */
		B3D_SCRIPT_EXPORT(ExportName(SkyMode), Property(Setter))
		void SetSkyMode(SkyMode mode)
		{
			mSkyMode = mode;
			MarkRenderProxyDataDirty();
		}

		/** @copydoc SetSkyMode */
		B3D_SCRIPT_EXPORT(ExportName(SkyMode), Property(Getter))
		SkyMode GetSkyMode() const { return mSkyMode; }

		/** Parameters controlling the procedural sky (Preetham). Ignored when #mSkyMode is Cubemap. */
		B3D_SCRIPT_EXPORT(ExportName(ProceduralSky), Property(Setter))
		void SetProceduralSky(const ProceduralSkyParams& params)
		{
			mProceduralSky = params;
			MarkRenderProxyDataDirty();
		}

		/** @copydoc SetProceduralSky */
		B3D_SCRIPT_EXPORT(ExportName(ProceduralSky), Property(Getter))
		const ProceduralSkyParams& GetProceduralSky() const { return mProceduralSky; }

		/** Returns the texture used for the skybox. */
		TextureType GetTexture() const { return mTexture; }

	protected:
		/** @copydoc CoreObject::MarkRenderProxyDataDirty */
		void MarkRenderProxyDataDirty(ComponentDirtyFlag flag = ComponentDirtyFlag::Everything);

		TextureType mTexture;
		float mBrightness = 1.0f; /**< Multiplier to apply to evaluated skybox values before using them. */
		SkyMode mSkyMode = SkyMode::Cubemap; /**< How the sky background is generated. */
		ProceduralSkyParams mProceduralSky; /**< Parameters for the procedural sky model. */
	};

	/** @} */

	/** @addtogroup Rendering
	 *  @{
	 */

	/** Allows you to specify an environment map to use for sampling radiance of the sky. */
	class B3D_EXPORT B3D_SCRIPT_EXPORT(DocumentationGroup(Rendering)) Skybox : public Component, public TSkybox<false>
	{
	public:
		Skybox(const HSceneObject& parent);

		/** @copydoc TSkybox::GetTexture */
		B3D_SCRIPT_EXPORT(ExportName(Texture), Property(Setter))
		void SetTexture(const HTexture& texture);

	protected:
		friend class render::Skybox;
		struct SyncPacket;

		/**
		 * Filters the skybox radiance texture, generating filtered radiance (for reflections) and irradiance. Should be
		 * called any time the skybox texture changes.
		 */
		void FilterTexture();

		TShared<render::RenderProxy> CreateRenderProxy() const override;
		RenderProxySyncPacket* CreateRenderProxySyncPacket(FrameAllocator& allocator, u32 flags) override;

		TShared<Texture> mFilteredRadiance;
		TShared<Texture> mIrradiance;
		TShared<render::RendererTask> mRendererTask;

		/************************************************************************/
		/* 						COMPONENT OVERRIDES                      		*/
		/************************************************************************/
	protected:
		friend class SceneObject;

		void Initialize() override;
		void OnCreated() override;
		void OnEnabled() override;
		void OnDisabled() override;
		void OnDestroyed() override;

		/************************************************************************/
		/* 								RTTI		                     		*/
		/************************************************************************/
	public:
		friend class SkyboxRTTI;
		static RTTIType* GetRttiStatic();
		RTTIType* GetRtti() const override;

	protected:
		Skybox(); // Serialization only
	};

	/** @} */

	/** @addtogroup Renderer
	 *  @{
	 */

	namespace render
	{
		/** Render thread counterpart of a b3d::Skybox */
		class B3D_EXPORT Skybox : public TSkybox<true>
		{
		public:
			~Skybox();

			/**
			 * Returns a texture containing filtered version of the radiance texture used for reflections. This might not
			 * be available if it hasn't been generated yet.
			 */
			TShared<Texture> GetFilteredRadiance() const { return mFilteredRadiance; }

		/**
		 * Returns a texture containing sky irradiance. This might not be available if it hasn't been generated yet.
		 */
		TShared<Texture> GetIrradiance() const { return mIrradiance; }

		/** @copydoc TSkybox::GetSkyMode */
		SkyMode GetSkyMode() const { return mSkyMode; }

		/** @copydoc TSkybox::GetProceduralSky */
		const ProceduralSkyParams& GetProceduralSky() const { return mProceduralSky; }

	protected:
		friend class b3d::Skybox;

			Skybox(const TShared<SceneInstance>& scene, const TShared<Texture>& radiance, const TShared<Texture>& filteredRadiance, const TShared<Texture>& irradiance);

			void Initialize() override;
			void SyncFromCoreObject(const CoreSyncData& data, FrameAllocator& allocator) override;

			TShared<Texture> mFilteredRadiance;
			TShared<Texture> mIrradiance;
			bool mActive = true;
			TShared<SceneInstance> mSceneInstance;
		};
	} // namespace render

	/** @} */
} // namespace b3d
