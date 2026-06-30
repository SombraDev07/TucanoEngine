//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup GUI
	 *  @{
	 */

	/// <summary>GUI element representing an element with a draggable handle of a variable size.</summary>
	[ShowInInspector]
	public partial class GUIScrollBar : GUIInteractable
	{
		private GUIScrollBar(bool __dummy0) { }
		protected GUIScrollBar() { }

		/// <summary>Position of the scroll handle in percent (ranging [0, 1]).</summary>
		[ShowInInspector]
		[NativeWrapper]
		public float ScrollHandlePosition
		{
			get { return Internal_GetScrollHandlePosition(mCachedPtr); }
			set { Internal_SetScrollHandlePosition(mCachedPtr, value); }
		}

		/// <summary>Size of the scroll handle in percent (ranging [0, 1]) of the total scroll bar area.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public float ScrollHandleSize
		{
			get { return Internal_GetScrollHandleSize(mCachedPtr); }
			set { Internal_SetScrollHandleSize(mCachedPtr, value); }
		}

		/// <summary>
		/// Triggered whenever the scrollbar handle is moved or resized. Values provided are the handle position and size in 
		/// percent (ranging [0, 1]).
		/// </summary>
		public event Action<float, float> OnScrollOrResize;

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetScrollHandlePosition(IntPtr thisPtr, float pct);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_GetScrollHandlePosition(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetScrollHandleSize(IntPtr thisPtr, float pct);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_GetScrollHandleSize(IntPtr thisPtr);
		private void Internal_OnScrollOrResize(float p0, float p1)
		{
			OnScrollOrResize?.Invoke(p0, p1);
		}
	}

	/** @} */
}
