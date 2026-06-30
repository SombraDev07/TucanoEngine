//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "B3DScriptObjectWrapper.h"
#include "../../../Engine/Core/Particles/B3DParticleEmitter.h"
#include "../../../Engine/Core/Particles/B3DParticleEmitter.h"
#include "../../../Engine/Utility/Math/B3DVector3.h"

namespace b3d
{
	struct __ParticleBoxShapeSettingsInterop
	{
		ParticleEmitterBoxType Type;
		TVector3<float> Extents;
	};

	class B3D_SCRIPT_INTEROP_EXPORT ScriptParticleBoxShapeSettings : public TScriptTypeDefinition<ScriptParticleBoxShapeSettings>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "ParticleBoxShapeSettings")

		static MonoObject* Box(const __ParticleBoxShapeSettingsInterop& value);
		static __ParticleBoxShapeSettingsInterop Unbox(MonoObject* value);
		static ParticleBoxShapeSettings FromInterop(const __ParticleBoxShapeSettingsInterop& value);
		static __ParticleBoxShapeSettingsInterop ToInterop(const ParticleBoxShapeSettings& value);

	private:
		ScriptParticleBoxShapeSettings();

	};
}
