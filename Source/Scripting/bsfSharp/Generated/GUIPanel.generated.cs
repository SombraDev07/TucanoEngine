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

	/// <summary>Represents a GUI panel that you can use for free placement of GUI elements within its bounds.</summary>
	[ShowInInspector]
	public partial class GUIPanel : GUILayout
	{
		private GUIPanel(bool __dummy0) { }
		protected GUIPanel() { }

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIPanel(GUIPanelContent contents, string styleClass, params GUIOption[] options)
		{
			Internal_Create(this, ref contents, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIPanel(GUIPanelContent contents, params GUIOption[] options)
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
		public GUIPanel(string styleClass, params GUIOption[] options)
		{
			Internal_Create1(this, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIPanel(params GUIOption[] options)
		{
			Internal_Create2(this, options);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUIPanel managedInstance, ref GUIPanelContent contents, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create0(GUIPanel managedInstance, ref GUIPanelContent contents, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create1(GUIPanel managedInstance, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create2(GUIPanel managedInstance, params GUIOption[] options);
	}

	/** @} */
}
