//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>
	/// Color represented as 4 components, each being a floating point value ranging from 0 to 1. Color components are red, 
	/// green, blue and alpha.
	/// </summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Color
	{
		public Color(float red = 1f, float green = 1f, float blue = 1f, float alpha = 1f)
		{
			this.R = red;
			this.G = green;
			this.B = blue;
			this.A = alpha;
		}

		public float R;
		public float G;
		public float B;
		public float A;
	}
}
