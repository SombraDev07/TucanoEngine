//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Input
     *  @{
     */
    public partial struct VirtualAxis
    {
        public static bool operator ==(VirtualAxis lhs, VirtualAxis rhs)
        {
            return lhs.AxisIdentifier == rhs.AxisIdentifier;
        }

        public static bool operator !=(VirtualAxis lhs, VirtualAxis rhs)
        {
            return !(lhs == rhs);
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return AxisIdentifier.GetHashCode();
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is VirtualAxis))
                return false;

            VirtualAxis otherAxis = (VirtualAxis)other;
            if (AxisIdentifier.Equals(otherAxis.AxisIdentifier))
                return true;

            return false;
        }
    }

    /** @} */
}
