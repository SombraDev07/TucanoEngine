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

	/// <summary>A collider represented by an arbitrary mesh.</summary>
	[ShowInInspector]
	public partial class MeshCollider : Collider
	{
		private MeshCollider(bool __dummy0) { }
		protected MeshCollider() { }

		/// <summary>
		/// Determines a mesh that represents the collider geometry. This can be a generic triangle mesh, or and convex mesh. 
		/// Triangle meshes are not supported as triggers, nor are they supported for colliders that are parts of a non-kinematic 
		/// rigidbody.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public RRef<PhysicsMesh> Mesh
		{
			get { return Internal_GetMesh(mCachedPtr); }
			set { Internal_SetMesh(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetMesh(IntPtr thisPtr, RRef<PhysicsMesh> mesh);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern RRef<PhysicsMesh> Internal_GetMesh(IntPtr thisPtr);
	}

	/** @} */
}
