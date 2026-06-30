//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    /// <summary>
    /// Axis aligned box represented by minimum and maximum point.
    /// </summary>
    public partial struct AABox
    {
        /// <summary>
        /// Returns the center of the box.
        /// </summary>
        public Vector3 Center
        {
            get
            { 		
                return new Vector3((Maximum.X + Minimum.X) * 0.5f,
                        (Maximum.Y + Minimum.Y) * 0.5f,
                        (Maximum.Z + Minimum.Z) * 0.5f);
            }
        }

        /// <summary>
        /// Returns the width, height and depth of the box.
        /// </summary>
        public Vector3 Size
        {
            get
            {
                return Maximum - Minimum;
            }
        }

        /// <summary>
        /// Creates a new axis aligned box.
        /// </summary>
        /// <param name="min">Corner of the box with minimum values.</param>
        /// <param name="max">Corner of the box with maximum values.</param>
        public AABox(Vector3 min, Vector3 max)
        {
            Minimum = min;
            Maximum = max;
        }

        /// <summary>
        /// Transforms the bounding box by the given matrix.
        ///
        /// As the resulting box will no longer be axis aligned, an axis align box is instead created by encompassing the
        /// transformed oriented bounding box. Retrieving the value as an actual OBB would provide a tighter fit.
        /// </summary>
        /// <param name="tfrm">Affine matrix to transform the box with.</param>
        public void TransformAffine(Matrix4 tfrm)
        {
            Vector3 center = Center;
            Vector3 halfSize = Size*0.5f;

            Vector3 newCenter = tfrm.MultiplyAffine(center);
            Vector3 newHalfSize = new Vector3(
                MathEx.Abs(tfrm.m00) * halfSize.X + MathEx.Abs(tfrm.m01) * halfSize.Y + MathEx.Abs(tfrm.m02) * halfSize.Z,
                MathEx.Abs(tfrm.m10) * halfSize.X + MathEx.Abs(tfrm.m11) * halfSize.Y + MathEx.Abs(tfrm.m12) * halfSize.Z,
                MathEx.Abs(tfrm.m20) * halfSize.X + MathEx.Abs(tfrm.m21) * halfSize.Y + MathEx.Abs(tfrm.m22) * halfSize.Z);

            Minimum = newCenter - newHalfSize;
            Maximum = newCenter + newHalfSize;
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            return "Min: " + Minimum + ". Max: " + Maximum;
        }
    };

    /** @} */
}
