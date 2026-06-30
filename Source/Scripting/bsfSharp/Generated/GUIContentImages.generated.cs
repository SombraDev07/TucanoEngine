//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup GUI
	 *  @{
	 */

	/// <summary>Contains separate GUI content images for every possible GUI element state.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUIContentImages
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static GUIContentImages Default()
		{
			GUIContentImages value = new GUIContentImages();
			value.Normal = null;
			value.Hover = null;
			value.Active = null;
			value.Focused = null;
			value.NormalOn = null;
			value.HoverOn = null;
			value.ActiveOn = null;
			value.FocusedOn = null;

			return value;
		}

		public GUIContentImages(SpriteImage image)
		{
			this.Normal = image;
			this.Hover = image;
			this.Active = image;
			this.Focused = image;
			this.NormalOn = image;
			this.HoverOn = image;
			this.ActiveOn = image;
			this.FocusedOn = image;
		}

		public SpriteImage Normal;
		public SpriteImage Hover;
		public SpriteImage Active;
		public SpriteImage Focused;
		public SpriteImage NormalOn;
		public SpriteImage HoverOn;
		public SpriteImage ActiveOn;
		public SpriteImage FocusedOn;
	}

	/** @} */
}
