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

	/// <summary>GUI element that may be inserted into layouts in order to make a space of a fixed size.</summary>
	[ShowInInspector]
	public partial class GUIFixedSpace : GUIElement
	{
		private GUIFixedSpace(bool __dummy0) { }
		protected GUIFixedSpace() { }

		/// <summary>Creates a new fixed space GUI element.</summary>
		public GUIFixedSpace(TUnitValue<int,LogicalPixel> size)
		{
			Internal_Create(this, ref size);
		}

		/// <summary>Changes the size of the space to the specified value, in pixels.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public TUnitValue<int,LogicalPixel> Size
		{
			get
			{
				TUnitValue<int,LogicalPixel> temp;
				Internal_GetSize(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetSize(mCachedPtr, ref value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetSize(IntPtr thisPtr, out TUnitValue<int,LogicalPixel> __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetSize(IntPtr thisPtr, ref TUnitValue<int,LogicalPixel> size);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUIFixedSpace managedInstance, ref TUnitValue<int,LogicalPixel> size);
	}

	/** @} */
}
