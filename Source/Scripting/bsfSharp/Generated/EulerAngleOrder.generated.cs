//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Values that represent in which order are euler angles applied when used in transformations.</summary>
	public enum EulerAngleOrder
	{
		XYZ = 0,
		ZXY = 4,
		YXZ = 2,
		XZY = 1,
		YZX = 3,
		ZYX = 5
	}
}
