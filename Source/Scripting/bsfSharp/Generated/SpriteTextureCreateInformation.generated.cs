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

	/// <summary>Descriptor structure used for initialization of a SpriteTexture.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SpriteTextureCreateInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SpriteTextureCreateInformation Default()
		{
			SpriteTextureCreateInformation value = new SpriteTextureCreateInformation();
			value.AtlasTexture = null;
			value.UVRange = TArea2<float,float>.Default();
			value.AnimationPlayback = SpriteAnimationPlayback.None;
			value.Animation = SpriteSheetGridAnimation.Default();

			return value;
		}

		/// <summary>Texture used as the atlas.</summary>
		public RRef<Texture> AtlasTexture;
		/// <summary>Range in the atlas texture that the image is to be read from, in [0, 1] range.</summary>
		public TArea2<float,float> UVRange;
		/// <summary>Determines if animation is enabled and how should it play.</summary>
		public SpriteAnimationPlayback AnimationPlayback;
		/// <summary>Describes the sprite sheet grid used for animation, if animation is used.</summary>
		public SpriteSheetGridAnimation Animation;
	}

	/** @} */
}
