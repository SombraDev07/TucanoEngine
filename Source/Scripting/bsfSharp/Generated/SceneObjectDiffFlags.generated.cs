//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Flags that mark which portion of a scene-object is modified.</summary>
	public enum SceneObjectDiffFlags
	{
		Name = 1,
		Position = 2,
		Active = 16,
		Rotation = 4,
		Scale = 8
	}
}
