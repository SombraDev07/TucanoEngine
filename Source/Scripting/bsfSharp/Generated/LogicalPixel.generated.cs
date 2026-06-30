//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>
	/// Location in 2D space in logical pixels. Logical pixels are defined at 1/96th of one logical inch. Logical pixels are 
	/// transformed into physical pixels by scaling it by the display&apos;s DPI scale. If your display is set to 96 DPI, then 
	/// one logical pixel equals one physical pixel.
	/// </summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct LogicalPixel
	{
	}
}
