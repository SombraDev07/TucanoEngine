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

	/// <summary>
	/// Specialization of a GUIScrollBar for horizontal scrolling, with the ability to resize the scroll bar.
	/// </summary>
	[ShowInInspector]
	public partial class GUIResizableHorizontalScrollBar : GUIScrollBar
	{
		private GUIResizableHorizontalScrollBar(bool __dummy0) { }
		protected GUIResizableHorizontalScrollBar() { }

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIResizableHorizontalScrollBar(string styleClass, params GUIOption[] options)
		{
			Internal_Create(this, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIResizableHorizontalScrollBar(params GUIOption[] options)
		{
			Internal_Create0(this, options);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUIResizableHorizontalScrollBar managedInstance, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create0(GUIResizableHorizontalScrollBar managedInstance, params GUIOption[] options);
	}

	/** @} */
}
