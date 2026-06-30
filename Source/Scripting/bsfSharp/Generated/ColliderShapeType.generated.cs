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

	/// <summary>Supported collider shapes.</summary>
	public enum ColliderShapeType
	{
		Plane = 0,
		Box = 1,
		Sphere = 2,
		Mesh = 4,
		Capsule = 3
	}

	/** @} */
}
