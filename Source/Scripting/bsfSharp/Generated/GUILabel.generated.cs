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

	/// <summary>GUI element that displays text and optionally a content image.</summary>
	[ShowInInspector]
	public partial class GUILabel : GUIInteractable
	{
		private GUILabel(bool __dummy0) { }
		protected GUILabel() { }

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUILabel(GUIContent contents, string styleClass, params GUIOption[] options)
		{
			Internal_Create(this, ref contents, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUILabel(GUIContent contents, params GUIOption[] options)
		{
			Internal_Create0(this, ref contents, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUILabel(string styleClass, params GUIOption[] options)
		{
			Internal_Create1(this, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUILabel(params GUIOption[] options)
		{
			Internal_Create2(this, options);
		}

		/// <summary>Changes the active content of the label.</summary>
		public void SetContent(GUIContent content)
		{
			Internal_SetContent(mCachedPtr, ref content);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetContent(IntPtr thisPtr, ref GUIContent content);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUILabel managedInstance, ref GUIContent contents, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create0(GUILabel managedInstance, ref GUIContent contents, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create1(GUILabel managedInstance, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create2(GUILabel managedInstance, params GUIOption[] options);
	}

	/** @} */
}
