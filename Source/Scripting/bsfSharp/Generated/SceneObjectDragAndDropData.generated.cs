//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Information about a single or multiple dragged scene objects.</summary>
	[ShowInInspector]
	public partial class SceneObjectDragAndDropData : DragAndDropData
	{
		private SceneObjectDragAndDropData(bool __dummy0) { }

		public SceneObjectDragAndDropData()
		{
			Internal_SceneObjectDragAndDropData(this);
		}

		public SceneObjectDragAndDropData(SceneObject sceneObject)
		{
			Internal_SceneObjectDragAndDropData0(this, sceneObject);
		}

		public SceneObjectDragAndDropData(SceneObject[] sceneObjects)
		{
			Internal_SceneObjectDragAndDropData1(this, sceneObjects);
		}

		[ShowInInspector]
		[NativeWrapper]
		public SceneObject[] SceneObjects
		{
			get { return Internal_GetSceneObjects(mCachedPtr); }
			set { Internal_SetSceneObjects(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SceneObjectDragAndDropData(SceneObjectDragAndDropData managedInstance);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SceneObjectDragAndDropData0(SceneObjectDragAndDropData managedInstance, SceneObject sceneObject);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SceneObjectDragAndDropData1(SceneObjectDragAndDropData managedInstance, SceneObject[] sceneObjects);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern SceneObject[] Internal_GetSceneObjects(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetSceneObjects(IntPtr thisPtr, SceneObject[] value);
	}
}
