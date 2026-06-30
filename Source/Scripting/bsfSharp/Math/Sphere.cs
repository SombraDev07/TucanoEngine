//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    /// <summary>
    /// A sphere represented by a center point and a radius.
    /// </summary>
    public partial struct Sphere
    {
        /// <summary>
        /// Creates a new sphere object.
        /// </summary>
        /// <param name="center">Center point of the sphere.</param>
        /// <param name="radius">Radius of the sphere.</param>
        public Sphere(Vector3 center, float radius)
        {
            Center = center;
            Radius = radius;
        }
    };

    /** @} */
}
