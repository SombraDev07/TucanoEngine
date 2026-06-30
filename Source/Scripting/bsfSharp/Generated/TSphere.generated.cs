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

	/// <summary>A sphere represented by a center point and a radius.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Sphere
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Sphere Default()
		{
			Sphere value = new Sphere();
			value.Radius = 0;
			value.Center = Vector3.Default();

			return value;
		}

		public float Radius;
		public Vector3 Center;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>A sphere represented by a center point and a radius.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SphereD
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SphereD Default()
		{
			SphereD value = new SphereD();
			value.Radius = 0;
			value.Center = Vector3D.Default();

			return value;
		}

		public double Radius;
		public Vector3D Center;
	}

	/** @} */
}
