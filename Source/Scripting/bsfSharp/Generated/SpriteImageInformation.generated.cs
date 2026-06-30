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

	/// <summary>Information about a SpriteImage.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SpriteImageInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SpriteImageInformation Default()
		{
			SpriteImageInformation value = new SpriteImageInformation();
			value.AnimationPlayback = SpriteAnimationPlayback.None;
			value.Animation = SpriteSheetGridAnimation.Default();

			return value;
		}

		/// <summary>Determines if animation is enabled and how should it play.</summary>
		public SpriteAnimationPlayback AnimationPlayback;
		/// <summary>Describes the sprite sheet grid used for animation, if animation is used.</summary>
		public SpriteSheetGridAnimation Animation;
	}

	/** @} */
}
