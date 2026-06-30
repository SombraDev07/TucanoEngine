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

	/// <summary>Structure describing contents of a GUIListBox element.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUIListBoxContent
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static GUIListBoxContent Default()
		{
			GUIListBoxContent value = new GUIListBoxContent();
			value.Elements = null;
			value.AllowMultiselect = false;

			return value;
		}

		public GUIListBoxContent(LocString[] elements, bool allowMultiselect = false)
		{
			this.Elements = elements;
			this.AllowMultiselect = allowMultiselect;
		}

		/// <summary>Elements to display in the list box.</summary>
		public LocString[] Elements;
		/// <summary>Determines should the listbox allow multiple elements to be selected or just one.</summary>
		public bool AllowMultiselect;
	}

	/** @} */
}
