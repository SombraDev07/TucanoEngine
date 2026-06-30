//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Determines how are elements positioned in a scroll area.</summary>
	public enum ScrollAreaLayoutType
	{
		/// <summary>
		/// GUI elements will be placed below/above each other (e.g. as in GUILayoutY). This is the standard scroll area.
		/// </summary>
		Vertical = 0,
		/// <summary>GUI elements will be placed left/right to each other (e.g. as in GUILayoutX).</summary>
		Horizontal = 1,
		/// <summary>GUI elements must be explicitly positioned (e.g. as in GUIPanel).</summary>
		Panel = 2
	}
}
