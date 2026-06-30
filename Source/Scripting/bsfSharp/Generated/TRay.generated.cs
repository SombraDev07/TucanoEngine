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

	/// <summary>A ray in 3D space represented with an origin and direction.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Ray
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Ray Default()
		{
			Ray value = new Ray();
			value.Origin = Vector3.Default();
			value.Direction = Vector3.Default();

			return value;
		}

		public Vector3 Origin;
		public Vector3 Direction;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>A ray in 3D space represented with an origin and direction.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct RayD
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static RayD Default()
		{
			RayD value = new RayD();
			value.Origin = Vector3D.Default();
			value.Direction = Vector3D.Default();

			return value;
		}

		public Vector3D Origin;
		public Vector3D Direction;
	}

	/** @} */
}
