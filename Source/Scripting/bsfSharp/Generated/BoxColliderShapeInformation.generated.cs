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

	/// <summary>Information describing box collider shape defined by its extents (half-size).</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct BoxColliderShapeInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static BoxColliderShapeInformation Default()
		{
			BoxColliderShapeInformation value = new BoxColliderShapeInformation();
			value.Extents = new Vector3(0.5f, 0.5f, 0.5f);

			return value;
		}

		public BoxColliderShapeInformation(Vector3 extents)
		{
			this.Extents = extents;
		}

		/// <summary>Half-size of the box (Distance from center to one side of the box).</summary>
		public Vector3 Extents;
	}

	/** @} */
}
