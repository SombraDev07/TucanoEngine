//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "B3DScriptObjectWrapper.h"
#include "../../../Engine/Core/Particles/B3DParticleEvolver.h"

namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptParticleTextureAnimationSettings : public TScriptTypeDefinition<ScriptParticleTextureAnimationSettings>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "ParticleTextureAnimationSettings")

		static MonoObject* Box(const ParticleTextureAnimationSettings& value);
		static ParticleTextureAnimationSettings Unbox(MonoObject* value);

	private:
		ScriptParticleTextureAnimationSettings();

	};
}
