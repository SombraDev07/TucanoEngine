//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Physics
	 *  @{
	 */

	/// <summary>Information describing a plane collider shape that extends infinitely in the X/Z axes.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct PlaneColliderShapeInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static PlaneColliderShapeInformation Default()
		{
			PlaneColliderShapeInformation value = new PlaneColliderShapeInformation();

			return value;
		}

	}

	/** @} */
}
