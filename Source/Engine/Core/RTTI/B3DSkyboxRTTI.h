//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Reflection/B3DRTTIType.h"
#include "Components/B3DSkybox.h"
#include "RTTI/B3DGameObjectRTTI.h"
#include "RTTI/B3DMathRTTI.h"

namespace b3d
{
	/** @cond RTTI */
	/** @addtogroup RTTI-Impl-Engine
	 *  @{
	 */

	class B3D_EXPORT SkyboxRTTI : public TRTTIType<Skybox, Component, SkyboxRTTI>
	{
	private:
		B3D_RTTI_BEGIN_MEMBERS
			B3D_RTTI_MEMBER(mTexture, 0)
			B3D_RTTI_MEMBER(mBrightness, 1)
			B3D_RTTI_MEMBER(mFilteredRadiance, 2)
			B3D_RTTI_MEMBER(mIrradiance, 3)
			B3D_RTTI_MEMBER(mSkyMode, 4)
			B3D_RTTI_MEMBER(mProceduralSky, 5)
		B3D_RTTI_END_MEMBERS

	public:
		const String& GetRttiName() override
		{
			static String name = "Skybox";
			return name;
		}

		u32 GetRttiId() const override
		{
			return TID_Skybox;
		}

		TShared<IReflectable> NewRttiObject() override
		{
			return SceneObject::CreateEmptyComponent<Skybox>();
		}
	};

	class B3D_EXPORT ProceduralSkyParamsRTTI : public TRTTIType<ProceduralSkyParams, IReflectable, ProceduralSkyParamsRTTI>
	{
	private:
		B3D_RTTI_BEGIN_MEMBERS
			B3D_RTTI_MEMBER(SunDirection, 0)
			B3D_RTTI_MEMBER(Rayleigh, 1)
			B3D_RTTI_MEMBER(Turbidity, 2)
			B3D_RTTI_MEMBER(MieCoefficient, 3)
			B3D_RTTI_MEMBER(MieDirectionalG, 4)
			B3D_RTTI_MEMBER(Luminance, 5)
		B3D_RTTI_END_MEMBERS

	public:
		const String& GetRttiName() override
		{
			static String name = "ProceduralSkyParams";
			return name;
		}

		u32 GetRttiId() const override
		{
			return TID_ProceduralSkyParams;
		}

		TShared<IReflectable> NewRttiObject() override
		{
			return B3DMakeShared<ProceduralSkyParams>();
		}
	};

	/** @} */
	/** @endcond */
} // namespace b3d
