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

	/// <summary>Axis aligned box represented by minimum and maximum point.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct AABox
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static AABox Default()
		{
			AABox value = new AABox();
			value.Minimum = Vector3.Default();
			value.Maximum = Vector3.Default();

			return value;
		}

		public Vector3 Minimum;
		public Vector3 Maximum;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>Axis aligned box represented by minimum and maximum point.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct AABoxD
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static AABoxD Default()
		{
			AABoxD value = new AABoxD();
			value.Minimum = Vector3D.Default();
			value.Maximum = Vector3D.Default();

			return value;
		}

		public Vector3D Minimum;
		public Vector3D Maximum;
	}

	/** @} */
}
