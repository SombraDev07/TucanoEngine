//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Options used to control texture mip map generation.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct MipMapGenOptions
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static MipMapGenOptions Default()
		{
			MipMapGenOptions value = new MipMapGenOptions();
			value.Filter = MipMapFilter.Box;
			value.WrapMode = MipMapWrapMode.Mirror;
			value.IsNormalMap = false;
			value.NormalizeMipmaps = false;
			value.IsSrgb = false;

			return value;
		}

		public MipMapFilter Filter;
		public MipMapWrapMode WrapMode;
		public bool IsNormalMap;
		public bool NormalizeMipmaps;
		public bool IsSrgb;
	}
}
