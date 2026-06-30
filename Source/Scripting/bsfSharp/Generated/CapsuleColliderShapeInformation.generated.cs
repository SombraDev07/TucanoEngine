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

	/// <summary>Information describing a capsule collider shape defined by its radius and half-height.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct CapsuleColliderShapeInformation
	{
		public CapsuleColliderShapeInformation(float radius = 0.5f, float halfHeight = 0.5f)
		{
			this.Radius = radius;
			this.HalfHeight = halfHeight;
		}

		public float Radius;
		public float HalfHeight;
	}

	/** @} */
}
