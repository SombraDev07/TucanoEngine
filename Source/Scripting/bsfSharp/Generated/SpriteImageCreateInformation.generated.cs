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

	/// <summary>Descriptor structure used for initialization of a SpriteImage.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SpriteImageCreateInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SpriteImageCreateInformation Default()
		{
			SpriteImageCreateInformation value = new SpriteImageCreateInformation();
			value.AnimationPlayback = SpriteAnimationPlayback.None;
			value.Animation = SpriteSheetGridAnimation.Default();

			return value;
		}

		public SpriteImageCreateInformation(SpriteImageInformation other)
		{
			this.AnimationPlayback = SpriteAnimationPlayback.None;
			this.Animation = SpriteSheetGridAnimation.Default();
		}

		///<summary>
		/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.
		///</summary>
		public SpriteImageInformation GetBase()
		{
			SpriteImageInformation value;
			value.AnimationPlayback = AnimationPlayback;
			value.Animation = Animation;
			return value;
		}

		///<summary>
		/// Assigns values to a subset of fields of this struct. This subset usually contains common field shared with 
		/// another struct.
		///</summary>
		public void SetBase(SpriteImageInformation value)
		{
			AnimationPlayback = value.AnimationPlayback;
			Animation = value.Animation;
		}

		/// <summary>Determines if animation is enabled and how should it play.</summary>
		public SpriteAnimationPlayback AnimationPlayback;
		/// <summary>Describes the sprite sheet grid used for animation, if animation is used.</summary>
		public SpriteSheetGridAnimation Animation;
	}

	/** @} */
}
