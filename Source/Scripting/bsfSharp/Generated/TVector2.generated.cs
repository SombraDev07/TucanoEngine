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

	/// <summary>A two dimensional vector.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct TVector2<T>
	{
		public T X;
		public T Y;
	}

	/** @} */

}
