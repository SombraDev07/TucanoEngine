//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Rendering
	 *  @{
	 */

	/// <summary>
	/// A top level container for all types of GUI elements. Every GUI element, layout or area must be assigned to a widget in 
	/// order to be rendered.
	///
	/// Widgets are the only GUI objects that may be arbitrarily transformed, allowing you to create 3D interfaces.
	/// </summary>
	[ShowInInspector]
	public partial class GUIWidget : Component
	{
		private GUIWidget(bool __dummy0) { }
		protected GUIWidget() { }

		/// <summary>Returns the root GUI panel for the widget.</summary>
		[NativeWrapper]
		public GUIPanel Panel
		{
			get { return Internal_GetPanel(mCachedPtr); }
		}

		/// <summary>
		/// Determines the depth to render the widget at. If two widgets overlap the widget with the lower depth will be rendered 
		/// in front.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public byte Depth
		{
			get { return Internal_GetDepth(mCachedPtr); }
			set { Internal_SetDepth(mCachedPtr, value); }
		}

		/// <summary>
		/// Checks are the specified coordinates within widget bounds. Coordinates should be relative to the parent window.
		/// </summary>
		public bool InBounds(TVector2<TUnitValue<int,PhysicalPixel>> position)
		{
			return Internal_InBounds(mCachedPtr, ref position);
		}

		/// <summary>Returns bounds of the widget, relative to the parent window.</summary>
		public TArea2<TUnitValue<int,PhysicalPixel>,TUnitValue<int,PhysicalPixel>> GetBounds()
		{
			TArea2<TUnitValue<int,PhysicalPixel>,TUnitValue<int,PhysicalPixel>> temp;
			Internal_GetBounds(mCachedPtr, out temp);
			return temp;
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern GUIPanel Internal_GetPanel(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetDepth(IntPtr thisPtr, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern byte Internal_GetDepth(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern bool Internal_InBounds(IntPtr thisPtr, ref TVector2<TUnitValue<int,PhysicalPixel>> position);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetBounds(IntPtr thisPtr, out TArea2<TUnitValue<int,PhysicalPixel>,TUnitValue<int,PhysicalPixel>> __output);
	}

	/** @} */
}
