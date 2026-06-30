//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "B3DScriptGUIElementWrapper.h"
#include "B3DScriptGUIScrollBar.generated.h"
#include "../../../Engine/Core/GUI/B3DGUIHorizontalScrollBar.h"
#include "../../../Engine/Core/GUI/B3DGUIOptions.h"

namespace b3d { class GUIHorizontalScrollBar; }
namespace b3d { struct __GUIOptionInterop; }
namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptGUIHorizontalScrollBar : public TScriptGUIElementWrapper<GUIHorizontalScrollBar, ScriptGUIHorizontalScrollBar, ScriptGUIScrollBarWrapperBase>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "GUIHorizontalScrollBar")

		ScriptGUIHorizontalScrollBar(GUIHorizontalScrollBar* nativeObject);
		~ScriptGUIHorizontalScrollBar();

		static void SetupScriptBindings();

		static MonoObject* CreateScriptObject(bool construct);

	private:
		static void InternalCreate(MonoObject* scriptObject, MonoString* styleClass, MonoArray* options);
		static void InternalCreate0(MonoObject* scriptObject, MonoArray* options);
	};
}
