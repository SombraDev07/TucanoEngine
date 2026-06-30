//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "B3DScriptGUIElementWrapper.h"
#include "B3DScriptGUIInteractable.generated.h"
#include "../../../Engine/Core/GUI/B3DGUIContent.h"
#include "../../../Engine/Core/GUI/B3DGUILabel.h"
#include "../../../Engine/Core/GUI/B3DGUIOptions.h"

namespace b3d { class GUILabel; }
namespace b3d { struct __GUIContentInterop; }
namespace b3d { struct __GUIOptionInterop; }
namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptGUILabel : public TScriptGUIElementWrapper<GUILabel, ScriptGUILabel, ScriptGUIInteractableWrapperBase>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "GUILabel")

		ScriptGUILabel(GUILabel* nativeObject);
		~ScriptGUILabel();

		static void SetupScriptBindings();

		static MonoObject* CreateScriptObject(bool construct);

	private:
		static void InternalSetContent(ScriptGUILabel* self, __GUIContentInterop* content);
		static void InternalCreate(MonoObject* scriptObject, __GUIContentInterop* contents, MonoString* styleClass, MonoArray* options);
		static void InternalCreate0(MonoObject* scriptObject, __GUIContentInterop* contents, MonoArray* options);
		static void InternalCreate1(MonoObject* scriptObject, MonoString* styleClass, MonoArray* options);
		static void InternalCreate2(MonoObject* scriptObject, MonoArray* options);
	};
}
