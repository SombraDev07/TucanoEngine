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

	/// <summary>Structure describing contents of a GUISliderHandle element.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUISliderHandleContent
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static GUISliderHandleContent Default()
		{
			GUISliderHandleContent value = new GUISliderHandleContent();
			value.Flags = (GUISliderHandleFlag)0;

			return value;
		}

		public GUISliderHandleContent(GUISliderHandleFlag flags)
		{
			this.Flags = flags;
		}

		/// <summary>Flags that control how does the slider handle behave.</summary>
		public GUISliderHandleFlag Flags;
	}

	/** @} */
}
