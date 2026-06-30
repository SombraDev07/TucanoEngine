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

	/// <summary>Information describing a mesh collider shape.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct MeshColliderShapeInformation
	{
		public MeshColliderShapeInformation(RRef<PhysicsMesh> mesh = null)
		{
			this.Mesh = mesh;
		}

		public RRef<PhysicsMesh> Mesh;
	}

	/** @} */
}
