//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System.Runtime.CompilerServices;

namespace b3d
{
    /** @addtogroup GUI_Engine
     *  @{
     */

    public partial struct GUIContent
    {
        /// <summary>
        /// Returns image content (if any).
        /// </summary>
        public SpriteImage GetImage(GUIElementState state = GUIElementState.Normal)
        {
            switch (state)
            {
            case GUIElementState.Normal:
                return Images.Normal;
            case GUIElementState.Hover:
                return Images.Hover;
            case GUIElementState.Active:
                return Images.Active;
            case GUIElementState.Focused:
                return Images.Focused;
            case GUIElementState.NormalOn:
                return Images.NormalOn;
            case GUIElementState.HoverOn:
                return Images.HoverOn;
            case GUIElementState.ActiveOn:
                return Images.ActiveOn;
            case GUIElementState.FocusedOn:
                return Images.FocusedOn;
            default:
                return Images.Normal;
            }
        }

        /// <summary>
        /// Implicitly converts a localized string into a GUI content containing only text.
        /// </summary>
        /// <param name="text">Localized string to initialize the GUI content with.</param>
        /// <returns>GUI content containing only a string.</returns>
        public static implicit operator GUIContent(LocString text)
        {
            return new GUIContent(text);
        }

        /// <summary>
        /// Implicitly converts a string into a GUI content containing only text.
        /// </summary>
        /// <param name="text">String to initialize the GUI content with.</param>
        /// <returns>GUI content containing only a string.</returns>
        public static implicit operator GUIContent(string text)
        {
            return new GUIContent(new LocString(text));
        }
    }

    public partial struct GUIContentImages
    {
        /// <summary>
        /// Creates a new object where content images for on and off states are different.
        /// </summary>
        /// <param name="imageOff">Image to assign to all off states.</param>
        /// <param name="imageOn">Image to assign to all on states.</param>
        public GUIContentImages(SpriteImage imageOff, SpriteImage imageOn)
        {
            Normal = imageOff;
            Hover = imageOff;
            Active = imageOff;
            Focused = imageOff;
            NormalOn = imageOn;
            HoverOn = imageOn;
            ActiveOn = imageOn;
            FocusedOn = imageOn;
        }

        /// <summary>
        /// Implicitly converts a sprite texture into a GUI content images object.
        /// </summary>
        /// <param name="image">Image to instantiate the GUI content images with.</param>
        /// <returns>GUI content images with all states set to the provided image.</returns>
        public static implicit operator GUIContentImages(SpriteImage image)
        {
            return new GUIContentImages(image);
        }
    }

    /** @} */
}
