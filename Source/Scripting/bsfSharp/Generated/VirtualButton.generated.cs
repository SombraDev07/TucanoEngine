//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Input
	 *  @{
	 */

	/// <summary>
	/// Identifier for a virtual button.
	///
	/// Primary purpose of this class is to avoid expensive string compare, and instead use a unique button identifier for 
	/// compare. Generally you want to create one of these using the button name, and then store it for later use.
	/// </summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct VirtualButton
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static VirtualButton Default()
		{
			VirtualButton value = new VirtualButton();
			value.ButtonIdentifier = 0;

			return value;
		}

		public int ButtonIdentifier;
	}

	/** @} */
}
