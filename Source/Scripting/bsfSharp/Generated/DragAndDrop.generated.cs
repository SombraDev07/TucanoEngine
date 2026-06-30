//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>
	/// Handles GUI drag and drop operations. When active GUI elements will be notified of any drag events and will be able to 
	/// retrieve dragged data. When cursor is released the data will be dropped and underlying GUI elements will be notified 
	/// the data is dropped. The dropped data is also available for a single frame via GetDropData() method.
	/// </summary>
	[ShowInInspector]
	public partial class DragAndDrop : ScriptObject
	{
		private DragAndDrop(bool __dummy0) { }
		protected DragAndDrop() { }

		/// <summary>Returns true if drag is currently in progress.</summary>
		[NativeWrapper]
		public static bool IsDragInProgress
		{
			get { return Internal_IsDragInProgress(); }
		}

		/// <summary>Returns true if a drop operation happened this frame.</summary>
		[NativeWrapper]
		public static bool IsDropInProgress
		{
			get { return Internal_IsDropInProgress(); }
		}

		/// <summary>Gets drag specific data specified when the drag started. Only valid if drag is in progress.</summary>
		[NativeWrapper]
		public static DragAndDropData DragData
		{
			get { return Internal_GetDragData(); }
		}

		/// <summary>
		/// Gets drag specific data specified when the drag started. Only valid if drop is in progress. This is only valid for a 
		/// single frame when a drop happens.
		/// </summary>
		[NativeWrapper]
		public static DragAndDropData DropData
		{
			get { return Internal_GetDropData(); }
		}

		/// <summary>
		/// Starts a drag operation with the specified data. This means GUI elements will start receiving drag and drop related 
		/// events and they may choose to handle them.
		/// </summary>
		/// <param name="data">Some operation specific data that is just passed through to however needs it.</param>
		public static void StartDrag(DragAndDropData data)
		{
			Internal_StartDrag(data);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_StartDrag(DragAndDropData data);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern bool Internal_IsDragInProgress();
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern bool Internal_IsDropInProgress();
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern DragAndDropData Internal_GetDragData();
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern DragAndDropData Internal_GetDropData();
	}
}
