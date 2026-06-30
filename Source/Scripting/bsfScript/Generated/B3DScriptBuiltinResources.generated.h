//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DScriptEnginePrerequisites.h"
#include "../../../Engine/Core/Resources/B3DBuiltinResources.h"
#include "B3DScriptTypeDefinition.h"
#include "../../../Engine/Core/Resources/B3DBuiltinResources.h"
#include "../../../Engine/Core/Resources/B3DBuiltinResources.h"
#include "../../../Engine/Core/Resources/B3DBuiltinResources.h"

namespace b3d
{
	class B3D_SCRIPT_INTEROP_EXPORT ScriptBuiltinResources : public TScriptTypeDefinition<ScriptBuiltinResources>
	{
	public:
		B3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, "BuiltinResources")

		ScriptBuiltinResources();

		static void SetupScriptBindings();

	private:
		static MonoObject* InternalGetWhiteSpriteTexture();
		static MonoObject* InternalGetBuiltinShader(BuiltinShader type);
		static MonoObject* InternalGetMesh(BuiltinMesh mesh);
		static MonoObject* InternalGetShader(MonoString* path);
		static MonoObject* InternalGetFont(MonoString* font);
		static MonoObject* InternalGetOrCompileShader(MonoString* path);
		static MonoObject* InternalGetDefaultFont();
		static MonoObject* InternalGetTexture(BuiltinTexture type);
	};
}
