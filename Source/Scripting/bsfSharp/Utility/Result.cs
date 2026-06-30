//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;

namespace b3d
{
    /** @addtogroup Utility
     *  @{
     */

    public partial struct Result
    {
        /** Returns true if the result is one of the success states. */
        public bool IsSuccessful => Status == ResultStatus.Succeeded;
    }

    /** @} */
}
