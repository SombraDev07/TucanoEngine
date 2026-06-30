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

	/// <summary>Structure describing contents of a GUIScrollArea element.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUIScrollAreaContent
	{
		public GUIScrollAreaContent(ScrollBarType verticalScrollBarType = ScrollBarType.ShowIfDoesntFit, ScrollBarType horizontalScrollBarType = ScrollBarType.NeverShow, ScrollAreaLayoutType layoutType = ScrollAreaLayoutType.Vertical)
		{
			this.VerticalScrollBarType = verticalScrollBarType;
			this.HorizontalScrollBarType = horizontalScrollBarType;
			this.LayoutType = layoutType;
		}

		public ScrollBarType VerticalScrollBarType;
		public ScrollBarType HorizontalScrollBarType;
		public ScrollAreaLayoutType LayoutType;
	}

	/** @} */
}
