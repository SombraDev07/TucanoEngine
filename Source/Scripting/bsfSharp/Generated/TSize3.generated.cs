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

	/// <summary>Encapsulates width/height/depth in a single structure.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Size3
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Size3 Default()
		{
			Size3 value = new Size3();
			value.Width = 0;
			value.Height = 0;
			value.Depth = 0;

			return value;
		}

		public Size3(float width, float height, float depth)
		{
			this.Width = 0;
			this.Height = 0;
			this.Depth = 0;
		}

		public float Width;
		public float Height;
		public float Depth;
	}

	/** @} */

	/** @addtogroup Math
	 *  @{
	 */

	/// <summary>Encapsulates width/height/depth in a single structure.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Size3UI
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Size3UI Default()
		{
			Size3UI value = new Size3UI();
			value.Width = 0;
			value.Height = 0;
			value.Depth = 0;

			return value;
		}

		public Size3UI(int width, int height, int depth)
		{
			this.Width = 0;
			this.Height = 0;
			this.Depth = 0;
		}

		public int Width;
		public int Height;
		public int Depth;
	}

	/** @} */
}
