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

    public partial struct VirtualButton
    {
        public static bool operator ==(VirtualButton lhs, VirtualButton rhs)
        {
            return lhs.ButtonIdentifier == rhs.ButtonIdentifier;
        }

        public static bool operator !=(VirtualButton lhs, VirtualButton rhs)
        {
            return !(lhs == rhs);
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return ButtonIdentifier.GetHashCode();
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is VirtualButton))
                return false;

            VirtualButton otherBtn = (VirtualButton)other;
            if (ButtonIdentifier.Equals(otherBtn.ButtonIdentifier))
                return true;

            return false;
        }
    }

    /** @} */
}
