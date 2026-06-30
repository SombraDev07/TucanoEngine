//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "../../../Engine/Core/Platform/B3DCursor.h"
#include "B3DScriptTypeDefinition.h"
#include "../../../Engine/Utility/Math/B3DArea2.h"
#include "../../../Engine/Core/GUI/B3DGUIUnits.h"
#include "../../../Engine/Core/Image/B3DPixelData.h"
#include "../../../Engine/Core/Utility/B3DEnums.h"
#include "../../../Engine/Utility/Math/B3DVector2.h"

namespace b3d { struct __TVector2_TUnitValue_int32_t__PhysicalPixel__Interop; }
namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptCursor : public TScriptTypeDefinition<ScriptCursor>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "Cursor")

		ScriptCursor();

		static void SetupScriptBindings();

	private:
		static void InternalSetScreenPosition(__TVector2_TUnitValue_int32_t__PhysicalPixel__Interop* screenPos);
		static void InternalGetScreenPosition(__TVector2_TUnitValue_int32_t__PhysicalPixel__Interop* __output);
		static void InternalHide();
		static void InternalShow();
		static void InternalClipToRect(TArea2<int32_t, uint32_t>* screenRect);
		static void InternalClipDisable();
		static void InternalSetCursor(CursorType type);
		static void InternalSetCursor0(MonoString* name);
		static void InternalSetCursorIcon(MonoString* name, MonoObject* pixelData, TVector2<int32_t>* hotSpot);
		static void InternalSetCursorIcon0(CursorType type, MonoObject* pixelData, TVector2<int32_t>* hotSpot);
		static void InternalClearCursorIcon(MonoString* name);
		static void InternalClearCursorIcon0(CursorType type);
	};
}
