//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>A plane represented by a normal and a distance.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Plane
	{
		public Vector3 Normal;
		public float D;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>A plane represented by a normal and a distance.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct PlaneD
	{
		public Vector3D Normal;
		public double D;
	}

	/** @} */
}
