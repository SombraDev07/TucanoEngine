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

	/// <summary>Kerning pair representing larger or smaller offset between a specific pair of characters.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct KerningPair
	{
		public int OtherCharId;
		public float Amount;
	}

	/** @} */
}
