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

	/// <summary>Represents a 2D area. Area is represented with an origin in top left and width/height.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct TArea2<PositionType, SizeType>
	{
		public PositionType X;
		public PositionType Y;
		public SizeType Width;
		public SizeType Height;
	}

	/** @} */

}
