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

	/// <summary>Information about a collision between two physics objects.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct CollisionData
	{
		/// <summary>Components of the colliders that have collided.</summary>
		public Collider[] Collider;
		/// <summary>Shapes of that have collided.</summary>
		public ColliderShape[] ColliderShapes;
		/// <summary>Information about all the contact points for the hit.</summary>
		public ContactPoint[] ContactPoints;
	}

	/** @} */
}
