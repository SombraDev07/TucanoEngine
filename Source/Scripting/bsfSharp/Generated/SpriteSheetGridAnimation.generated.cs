//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Rendering
	 *  @{
	 */

	/// <summary>
	/// Descriptor that describes a simple sprite sheet animation. The parent area is split into a grid of <see 
	/// cref="RowCount"/> x <see cref="ColumnCount"/>, each representing one frame of the animation. Every frame is of equal 
	/// size. Frames are sequentially evaluated starting from the top-most row, iterating over all columns in a row and then 
	/// moving to next row, up to <see cref="FrameCount"/> frames. <see cref="FramesPerSecond"/> frames are evaluated every 
	/// second, allowing you to control animation speed.
	/// </summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SpriteSheetGridAnimation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SpriteSheetGridAnimation Default()
		{
			SpriteSheetGridAnimation value = new SpriteSheetGridAnimation();
			value.RowCount = 1;
			value.ColumnCount = 1;
			value.FrameCount = 1;
			value.FramesPerSecond = 8;

			return value;
		}

		public SpriteSheetGridAnimation(int rowCount, int columnCount, int frameCount, int framesPerSecond)
		{
			this.RowCount = rowCount;
			this.ColumnCount = columnCount;
			this.FrameCount = frameCount;
			this.FramesPerSecond = framesPerSecond;
		}

		/// <summary>
		/// Number of rows to divide the parent area in. Determines height of the individual frame (depends on parent area size).
		/// </summary>
		public int RowCount;
		/// <summary>
		/// Number of columns to divide the parent area in. Determines column of the individual frame (depends on parent area 
		/// size).
		/// </summary>
		public int ColumnCount;
		/// <summary>
		/// Number of frames in the animation. Must be less or equal than <see cref="RowCount"/> * <see cref="ColumnCount"/>.
		/// </summary>
		public int FrameCount;
		/// <summary>How many frames to evaluate each second. Determines the animation speed.</summary>
		public int FramesPerSecond;
	}

	/** @} */
}
