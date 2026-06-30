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

	/// <summary>Descriptor structure used for initialization of a Font.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct FontCreateInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static FontCreateInformation Default()
		{
			FontCreateInformation value = new FontCreateInformation();
			value.Name = "";
			value.DPI = 96;
			value.RenderMode = FontRenderMode.HintedSmooth;

			return value;
		}

		public FontCreateInformation(FontInformation other)
		{
			this.Name = "";
			this.DPI = 96;
			this.RenderMode = FontRenderMode.HintedSmooth;
		}

		///<summary>
		/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.
		///</summary>
		public FontInformation GetBase()
		{
			FontInformation value;
			value.Name = Name;
			value.DPI = DPI;
			value.RenderMode = RenderMode;
			return value;
		}

		///<summary>
		/// Assigns values to a subset of fields of this struct. This subset usually contains common field shared with 
		/// another struct.
		///</summary>
		public void SetBase(FontInformation value)
		{
			Name = value.Name;
			DPI = value.DPI;
			RenderMode = value.RenderMode;
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
