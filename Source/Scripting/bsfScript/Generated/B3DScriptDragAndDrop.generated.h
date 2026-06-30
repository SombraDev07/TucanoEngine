//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "../../../Engine/Core/GUI/B3DDragAndDrop.h"
#include "B3DScriptTypeDefinition.h"

namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptDragAndDrop : public TScriptTypeDefinition<ScriptDragAndDrop>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "DragAndDrop")

		ScriptDragAndDrop();

		static void SetupScriptBindings();

	private:
		static void InternalStartDrag(MonoObject* data);
		static bool InternalIsDragInProgress();
		static bool InternalIsDropInProgress();
		static MonoObject* InternalGetDragData();
		static MonoObject* InternalGetDropData();
	};
}
