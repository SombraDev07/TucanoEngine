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

	/// <summary>Information describing a sphere collider shape defined by is radius.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SphereColliderShapeInformation
	{
		public SphereColliderShapeInformation(float radius = 1f)
		{
			this.Radius = radius;
		}

		public float Radius;
	}

	/** @} */
}
