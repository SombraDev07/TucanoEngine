//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Resource meta-data for user-defined managed resources.</summary>
	[ShowInInspector]
	public partial class ManagedResourceMetaData : ResourceMetaData
	{
		private ManagedResourceMetaData(bool __dummy0) { }
		protected ManagedResourceMetaData() { }

		[ShowInInspector]
		[NativeWrapper]
		public string TypeNamespace
		{
			get { return Internal_GetTypeNamespace(mCachedPtr); }
			set { Internal_SetTypeNamespace(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public string TypeName
		{
			get { return Internal_GetTypeName(mCachedPtr); }
			set { Internal_SetTypeName(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string Internal_GetTypeNamespace(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetTypeNamespace(IntPtr thisPtr, string value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string Internal_GetTypeName(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetTypeName(IntPtr thisPtr, string value);
	}
}
