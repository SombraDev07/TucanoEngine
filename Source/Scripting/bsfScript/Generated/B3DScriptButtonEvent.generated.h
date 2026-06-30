//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "B3DScriptObjectWrapper.h"
#include "../../../Engine/Core/Input/B3DInputFwd.h"

namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptButtonEvent : public TScriptTypeDefinition<ScriptButtonEvent>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "ButtonEvent")

		static MonoObject* Box(const ButtonEvent& value);
		static ButtonEvent Unbox(MonoObject* value);

	private:
		ScriptButtonEvent();

	};
}
