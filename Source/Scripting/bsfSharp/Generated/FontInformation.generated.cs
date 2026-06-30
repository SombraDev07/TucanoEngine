//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Text
	 *  @{
	 */

	/// <summary>Information about a Font.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct FontInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static FontInformation Default()
		{
			FontInformation value = new FontInformation();
			value.Name = "";
			value.DPI = 96;
			value.RenderMode = FontRenderMode.HintedSmooth;

			return value;
		}

		/// <summary>Optional name of the font. Used primarily for easier debugging.</summary>
		public string Name;
		/// <summary>Determines dots per inch scale that will be used when rendering the characters.</summary>
		public int DPI;
		/// <summary>Determines the render mode used for rendering the characters into a bitmap.</summary>
		public FontRenderMode RenderMode;
	}

	/** @} */
}
