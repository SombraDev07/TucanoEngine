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

	/// <summary>Descriptor structure used for initialization of a SpriteGlyph.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct SpriteGlyphCreateInformation
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static SpriteGlyphCreateInformation Default()
		{
			SpriteGlyphCreateInformation value = new SpriteGlyphCreateInformation();
			value.Font = null;
			value.Glyph = 0;
			value.DefaultSize = 8f;
			value.AnimationPlayback = SpriteAnimationPlayback.None;
			value.Animation = SpriteSheetGridAnimation.Default();

			return value;
		}

		public SpriteGlyphCreateInformation(SpriteImageInformation spriteImageInformation, RRef<Font> font, int glyph, float defaultSize)
		{
			this.Font = font;
			this.Glyph = glyph;
			this.DefaultSize = defaultSize;
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

		/// <summary>Font from which to render the glyph from.</summary>
		public RRef<Font> Font;
		/// <summary>Unicode code for the glyph to render.</summary>
		public int Glyph;
		/// <summary>
		/// Size of the unscaled glyph in points. Actual rendered size might be different depending on DPI scale or other scale 
		/// factors.
		/// </summary>
		public float DefaultSize;
		/// <summary>Determines if animation is enabled and how should it play.</summary>
		public SpriteAnimationPlayback AnimationPlayback;
		/// <summary>Describes the sprite sheet grid used for animation, if animation is used.</summary>
		public SpriteSheetGridAnimation Animation;
	}

	/** @} */
}
