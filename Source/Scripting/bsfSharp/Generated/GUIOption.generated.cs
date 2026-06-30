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

	/// <summary>
	/// Controls GUI element layout options, possibly by overriding the default options specified in GUI element style. These 
	/// options control GUI element placement and size in a GUI layout.
	/// </summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct GUIOption
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static GUIOption Default()
		{
			GUIOption value = new GUIOption();
			value.mMinimum = new TUnitValue<int,LogicalPixel>(0);
			value.mMaximum = new TUnitValue<int,LogicalPixel>(0);
			value.mType = GUIOptionType.FixedWidth;

			return value;
		}

		public TUnitValue<int,LogicalPixel> mMinimum;
		public TUnitValue<int,LogicalPixel> mMaximum;
		public GUIOptionType mType;
	}

	/** @} */
}
