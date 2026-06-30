//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup GUI_Engine
     *  @{
     */

    public partial struct GUIOption
    {
        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element should be positioned at this offset from the
        /// parent GUI panel. This option is ignored if element is part of a layout since it controls its placement.
        /// </summary>
        /// <param name="x">Horizontal offset from the parent GUI panel origin, in pixels.</param>
        /// <param name="y">Vertical offset from the parent GUI panel origin, in pixels.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption Position(GUILogicalUnit x, GUILogicalUnit y)
        {
            GUIOption option = new GUIOption();
            option.mMinimum = x;
            option.mMaximum = y;
            option.mType = GUIOptionType.Position;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element has a fixed width. This will override the width
        /// property set in element style.
        /// </summary>
        /// <param name="width">Width in pixels.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption FixedWidth(GUILogicalUnit width)
        {
            GUIOption option = new GUIOption();
            option.mMinimum = option.mMaximum = width;
            option.mType = GUIOptionType.FixedWidth;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element has a fixed height. This will override the height
        /// property set in element style.
        /// </summary>
        /// <param name="height">Height in pixels.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption FixedHeight(GUILogicalUnit height)
        {
            GUIOption option = new GUIOption();
            option.mMinimum = option.mMaximum = height;
            option.mType = GUIOptionType.FixedHeight;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element has a flexible width with optional min/max
        /// constraints. Element will be resized according to its contents and parent layout but will always stay within the
        /// provided range.
        /// </summary>
        /// <param name="minWidth">Minimum width in pixels. Element will never be smaller than this width.</param>
        /// <param name="maxWidth">Maximum width in pixels. Element will never be larger than this width. Specify zero for
        ///                        unlimited width.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption FlexibleWidth(GUILogicalUnit minWidth = new GUILogicalUnit(), GUILogicalUnit maxWidth = new GUILogicalUnit())
        {
            GUIOption option = new GUIOption();
            option.mMinimum = minWidth;
            option.mMaximum = maxWidth;
            option.mType = GUIOptionType.FlexibleWidth;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element has a flexible height with optional min/max
        /// constraints. Element will be resized according to its contents and parent layout but will always stay within the
        /// provided range.
        /// </summary>
        /// <param name="minHeight">Minimum height in pixels. Element will never be smaller than this height.</param>
        /// <param name="maxHeight">Maximum height in pixels. Element will never be larger than this height. Specify zero for
        ///                          unlimited height.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption FlexibleHeight(GUILogicalUnit minHeight = new GUILogicalUnit(), GUILogicalUnit maxHeight = new GUILogicalUnit())
        {
            GUIOption option = new GUIOption();
            option.mMinimum = minHeight;
            option.mMaximum = maxHeight;
            option.mType = GUIOptionType.FlexibleHeight;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element will expand to fill the width of the parent element,
        /// while staying within the provided range.
        /// </summary>
        /// <param name="minWidth">Minimum height in width. Element will never be smaller than this width.</param>
        /// <param name="maxWidth">Maximum height in width. Element will never be larger than this width. Specify zero for unlimited width.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption ExpandableWidth(GUILogicalUnit minWidth = new GUILogicalUnit(), GUILogicalUnit maxWidth = new GUILogicalUnit())
        {
            GUIOption option = new GUIOption();
            option.mMinimum = minWidth;
            option.mMaximum = maxWidth;
            option.mType = GUIOptionType.ExpandingWidth;

            return option;
        }

        /// <summary>
        /// Constructs a GUI option notifying the GUI layout that this element will expand to fill the height of the parent element,
        /// while staying within the provided range.
        /// </summary>
        /// <param name="minHeight">Minimum height in pixels. Element will never be smaller than this height.</param>
        /// <param name="maxHeight">Maximum height in pixels. Element will never be larger than this height. Specify zero for unlimited height.</param>
        /// <returns>New option object that can be used for initializing a GUI element.</returns>
        public static GUIOption ExpandableHeight(GUILogicalUnit minHeight = new GUILogicalUnit(), GUILogicalUnit maxHeight = new GUILogicalUnit())
        {
            GUIOption option = new GUIOption();
            option.mMinimum = minHeight;
            option.mMaximum = maxHeight;
            option.mType = GUIOptionType.ExpandingHeight;

            return option;
        }
    }

    /** @} */
}
