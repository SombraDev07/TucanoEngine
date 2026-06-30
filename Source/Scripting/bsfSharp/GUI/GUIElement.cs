//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;

namespace b3d
{
    /** @addtogroup GUI_Engine
     *  @{
     */

    public partial class GUIElement
    {
        /// <summary>
        /// Returns the position of the GUI element relative to the first parent GUI panel. Values are provided in logical pixel units.
        /// </summary>
        /// <remarks>This call can be potentially expensive if the GUI state is dirty, as it can trigger a layout update operation.</remarks>
        public GUILogicalPoint LayoutCalculatedPositionRelativeToParentPanel => CalculatePositionRelativeTo(null);
    }

    /** @} */
}
