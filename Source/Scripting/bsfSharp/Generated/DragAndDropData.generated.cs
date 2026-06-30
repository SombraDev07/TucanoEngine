//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Base type that should be inherited to provide specific data relevant to a drag and drop operation.</summary>
	[ShowInInspector]
	public partial class DragAndDropData : ScriptObject
	{
		private DragAndDropData(bool __dummy0) { }

		public DragAndDropData()
		{
			Internal_DragAndDropData(this);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DragAndDropData(DragAndDropData managedInstance);
	}
}
