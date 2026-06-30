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

	/// <summary>Structure describing contents of a GUIInputBox element.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUIInputBoxContent
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static GUIInputBoxContent Default()
		{
			GUIInputBoxContent value = new GUIInputBoxContent();
			value.AllowMultiline = false;

			return value;
		}

		public GUIInputBoxContent(bool allowMultiline)
		{
			this.AllowMultiline = allowMultiline;
		}

		/// <summary>If true, allows multiline input.</summary>
		public bool AllowMultiline;
	}

	/** @} */
}
