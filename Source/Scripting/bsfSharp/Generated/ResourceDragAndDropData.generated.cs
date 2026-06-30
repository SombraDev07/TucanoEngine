//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Information about a single or multiple dragged resources, represented as paths.</summary>
	[ShowInInspector]
	public partial class ResourceDragAndDropData : DragAndDropData
	{
		private ResourceDragAndDropData(bool __dummy0) { }

		public ResourceDragAndDropData()
		{
			Internal_ResourceDragAndDropData(this);
		}

		public ResourceDragAndDropData(string relativeResourcePath)
		{
			Internal_ResourceDragAndDropData0(this, relativeResourcePath);
		}

		public ResourceDragAndDropData(string[] relativeResourcePaths)
		{
			Internal_ResourceDragAndDropData1(this, relativeResourcePaths);
		}

		[ShowInInspector]
		[NativeWrapper]
		public string[] RelativeResourcePaths
		{
			get { return Internal_GetRelativeResourcePaths(mCachedPtr); }
			set { Internal_SetRelativeResourcePaths(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_ResourceDragAndDropData(ResourceDragAndDropData managedInstance);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_ResourceDragAndDropData0(ResourceDragAndDropData managedInstance, string relativeResourcePath);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_ResourceDragAndDropData1(ResourceDragAndDropData managedInstance, string[] relativeResourcePaths);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string[] Internal_GetRelativeResourcePaths(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetRelativeResourcePaths(IntPtr thisPtr, string[] value);
	}
}
