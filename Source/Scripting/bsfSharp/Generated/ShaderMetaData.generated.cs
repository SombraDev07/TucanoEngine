//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Shader specific resource meta-data containing information about referenced include files.</summary>
	[ShowInInspector]
	public partial class ShaderMetaData : ResourceMetaData
	{
		private ShaderMetaData(bool __dummy0) { }
		protected ShaderMetaData() { }

		[ShowInInspector]
		[NativeWrapper]
		public string[] Includes
		{
			get { return Internal_GetIncludes(mCachedPtr); }
			set { Internal_SetIncludes(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string[] Internal_GetIncludes(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetIncludes(IntPtr thisPtr, string[] value);
	}
}
