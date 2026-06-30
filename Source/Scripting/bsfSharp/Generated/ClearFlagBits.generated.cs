//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Flags that determine which portion of the viewport to clear.</summary>
	public enum ClearFlags
	{
		Empty = 0,
		Color = 1,
		Depth = 2,
		Stencil = 4
	}
}
