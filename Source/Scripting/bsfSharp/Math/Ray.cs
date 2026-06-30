//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    public partial struct Ray
    {
        /// <summary>
        /// Creates a new ray.
        /// </summary>
        /// <param name="origin">Coordinates for the origin of the ray.</param>
        /// <param name="direction">Normalized direction of the ray.</param>
        public Ray(Vector3 origin, Vector3 direction)
        {
            this.Origin = origin;
            this.Direction = direction;
        }

        /// <summary>
        /// Multiples ray by a scalar and retrieves a point along the ray.
        /// </summary>
        /// <param name="ray">Ray to transform.</param>
        /// <param name="t">How far along the ray to retrieve the point.</param>
        /// <returns>Point along the ray <paramref name="t"/> units away from the origin.</returns>
        public static Vector3 operator*(Ray ray, float t)
        {
            return ray.Origin + ray.Direction * t;
        }

        /// <summary>
        /// Transforms the ray by the specified matrix. If the matrix is affine use
        /// <see cref="TransformAffine"/> as it is faster.
        /// </summary>
        /// <param name="matrix">Matrix to transform the ray by.</param>
        public void Transform(Matrix4 matrix)
        {
            Vector3 end = this * 1.0f;

            Origin = matrix.Multiply(Origin);
            end = matrix.Multiply(end);

            Direction = Vector3.Normalize(end - Origin);
        }

        /// <summary>
        /// Transforms the ray by the specified affine matrix.
        /// </summary>
        /// <param name="matrix">Affine matrix to transform the ray by.</param>
        public void TransformAffine(Matrix4 matrix)
        {
            Vector3 end = this * 1.0f;

            Origin = matrix.MultiplyDirection(Origin);
            end = matrix.MultiplyDirection(end);

            Direction = Vector3.Normalize(end - Origin);
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            return String.Format("(origin: {0} direction: {1})", Origin, Direction);
        }
    };

    /** @} */
}
