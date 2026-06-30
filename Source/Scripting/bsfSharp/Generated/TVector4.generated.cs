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

	/// <summary>A four dimensional vector.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Vector4
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Vector4 Default()
		{
			Vector4 value = new Vector4();
			value.X = 0;
			value.Y = 0;
			value.Z = 0;
			value.W = 0;

			return value;
		}

		public Vector4(float x, float y, float z, float w)
		{
			this.X = x;
			this.Y = y;
			this.Z = z;
			this.W = w;
		}

		public float X;
		public float Y;
		public float Z;
		public float W;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>A four dimensional vector.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Vector4D
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Vector4D Default()
		{
			Vector4D value = new Vector4D();
			value.X = 0;
			value.Y = 0;
			value.Z = 0;
			value.W = 0;

			return value;
		}

		public Vector4D(double x, double y, double z, double w)
		{
			this.X = 0;
			this.Y = 0;
			this.Z = 0;
			this.W = 0;
		}

		public double X;
		public double Y;
		public double Z;
		public double W;
	}

	/** @} */
}
