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
	/// Input box is a GUI element that accepts Unicode textual input. It can be single or multi-line and handles various 
	/// types of text manipulation.
	/// </summary>
	[ShowInInspector]
	public partial class GUIInputBox : GUIInteractable
	{
		private GUIInputBox(bool __dummy0) { }
		protected GUIInputBox() { }

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIInputBox(GUIInputBoxContent contents, string styleClass, params GUIOption[] options)
		{
			Internal_Create(this, ref contents, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="contents">Structure describing the contents of the GUI element to create.</param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIInputBox(GUIInputBoxContent contents, params GUIOption[] options)
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
		public GUIInputBox(string styleClass, params GUIOption[] options)
		{
			Internal_Create1(this, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUIInputBox(params GUIOption[] options)
		{
			Internal_Create2(this, options);
		}

		/// <summary>Determines the text inside the input box.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public string Text
		{
			get { return Internal_GetText(mCachedPtr); }
			set { Internal_SetText(mCachedPtr, value); }
		}

		/// <summary>Triggered whenever input text has changed.</summary>
		public event Action<string> OnValueChanged;

		/// <summary>Triggered when the user hits the Enter key with the input box in focus.</summary>
		public event Action OnConfirm;

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetText(IntPtr thisPtr, string text);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string Internal_GetText(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUIInputBox managedInstance, ref GUIInputBoxContent contents, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create0(GUIInputBox managedInstance, ref GUIInputBoxContent contents, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create1(GUIInputBox managedInstance, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create2(GUIInputBox managedInstance, params GUIOption[] options);
		private void Internal_OnValueChanged(string p0)
		{
			OnValueChanged?.Invoke(p0);
		}
		private void Internal_OnConfirm()
		{
			OnConfirm?.Invoke();
		}
	}

	/** @} */
}
