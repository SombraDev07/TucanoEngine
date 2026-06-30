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

	/// <summary>Controls distance joint options.</summary>
	public enum DistanceJointFlag
	{
		/// <summary>Enables minimum distance limit.</summary>
		MinDistance = 1,
		/// <summary>Enables maximum distance limit.</summary>
		MaxDistance = 2,
		/// <summary>Enables spring when maintaining limits.</summary>
		Spring = 4
	}

	/** @} */
}
