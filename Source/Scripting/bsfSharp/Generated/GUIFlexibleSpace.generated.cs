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
	/// GUI element that may be inserted into layouts to make a space of a flexible size. The space will expand only if there 
	/// is room and other elements are not squished because of it. If multiple flexible spaces are in a layout, their sizes 
	/// will be shared equally.
	/// </summary>
	[ShowInInspector]
	public partial class GUIFlexibleSpace : GUIElement
	{
		private GUIFlexibleSpace(bool __dummy0) { }

		/// <summary>Creates a new flexible space GUI element.</summary>
		public GUIFlexibleSpace()
		{
			Internal_Create(this);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUIFlexibleSpace managedInstance);
	}

	/** @} */
}
