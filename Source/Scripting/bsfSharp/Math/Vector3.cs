//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    /// <summary>
    /// A three dimensional vector.
    /// </summary>
    public partial struct Vector3
    {
        public static readonly Vector3 Zero = new Vector3(0.0f, 0.0f, 0.0f);
        public static readonly Vector3 One = new Vector3(1.0f, 1.0f, 1.0f);
        public static readonly Vector3 XAxis = new Vector3(1.0f, 0.0f, 0.0f);
        public static readonly Vector3 YAxis = new Vector3(0.0f, 1.0f, 0.0f);
        public static readonly Vector3 ZAxis = new Vector3(0.0f, 0.0f, 1.0f);

        /// <summary>
        /// Accesses a specific component of the vector.
        /// </summary>
        /// <param name="index">Index of the component.</param>
        /// <returns>Value of the specific component.</returns>
        public float this[int index]
        {
            get
            {
                switch (index)
                {
                    case 0:
                        return X;
                    case 1:
                        return Y;
                    case 2:
                        return Z;
                    default:
                        throw new IndexOutOfRangeException("Invalid Vector3 index.");
                }
            }

            set
            {
                switch (index)
                {
                    case 0:
                        X = value;
                        break;
                    case 1:
                        Y = value;
                        break;
                    case 2:
                        Z = value;
                        break;
                    default:
                        throw new IndexOutOfRangeException("Invalid Vector3 index.");
                }
            }
        }

        /// <summary>
        /// Returns a normalized copy of the vector.
        /// </summary>
        public Vector3 Normalized
        {
            get
            {
                return Normalize(this);
            }
        }

        /// <summary>
        /// Returns the length of the vector.
        /// </summary>
        public float Length
        {
            get
            {
                return (float)MathEx.Sqrt(X * X + Y * Y + Z * Z);
            }
        }

        /// <summary>
        /// Returns the squared length of the vector.
        /// </summary>
        public float SqrdLength
        {
            get
            {
                return (X * X + Y * Y + Z * Z);
            }
        }

        /// <summary>
        /// Converts a three dimensional vector into a four dimensional vector. w component will be set to zero.
        /// </summary>
        /// <param name="vec">Vector to convert.</param>
        /// <returns>A new four dimensional vector.</returns>
        public static explicit operator Vector4(Vector3 vec)
        {
            return new Vector4(vec.X, vec.Y, vec.Z, 0.0f);
        }

        public static Vector3 operator+ (Vector3 a, Vector3 b)
        {
            return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        public static Vector3 operator- (Vector3 a, Vector3 b)
        {
            return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        public static Vector3 operator- (Vector3 v)
        {
            return new Vector3(-v.X, -v.Y, -v.Z);
        }

        public static Vector3 operator*(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
        }

        public static Vector3 operator* (Vector3 v, float d)
        {
            return new Vector3(v.X * d, v.Y * d, v.Z * d);
        }

        public static Vector3 operator* (float d, Vector3 v)
        {
            return new Vector3(v.X * d, v.Y * d, v.Z * d);
        }

        public static Vector3 operator/ (Vector3 v, float d)
        {
            return new Vector3(v.X / d, v.Y / d, v.Z / d);
        }

        public static bool operator ==(Vector3 lhs, Vector3 rhs)
        {
            return lhs.X == rhs.X && lhs.Y == rhs.Y && lhs.Z == rhs.Z;
        }

        public static bool operator !=(Vector3 lhs, Vector3 rhs)
        {
            return !(lhs == rhs);
        }

        /// <summary>
        /// Calculates the magnitude of the provided vector.
        /// </summary>
        /// <param name="v">Vector to calculate the magnitude for.</param>
        /// <returns>Magnitude of the vector.</returns>
        public static float Magnitude(Vector3 v)
        {
            return MathEx.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        }

        /// <summary>
        /// Calculates the squared magnitude of the provided vector.
        /// </summary>
        /// <param name="v">Vector to calculate the magnitude for.</param>
        /// <returns>Squared magnitude of the vector.</returns>
        public static float SqrMagnitude(Vector3 v)
        {
            return (v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        }

        /// <summary>
        /// Scales one vector by another.
        /// </summary>
        /// <param name="a">First three dimensional vector.</param>
        /// <param name="b">Second three dimensional vector.</param>
        /// <returns>One vector scaled by another.</returns>
        public static Vector3 Scale(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
        }

        /// <summary>
        /// Returns the cross product between two vectors.
        /// </summary>
        /// <param name="lhs">First three dimensional vector.</param>
        /// <param name="rhs">Second three dimensional vector.</param>
        /// <returns>Cross product between two vectors.</returns>
        public static Vector3 Cross(Vector3 lhs, Vector3 rhs)
        {
            return new Vector3(lhs.Y * rhs.Z - lhs.Z * rhs.Y, lhs.Z * rhs.X - lhs.X * rhs.Z, lhs.X * rhs.Y - lhs.Y * rhs.X);
        }

        /// <summary>
        /// Normalizes the provided vector and returns the normalized copy.
        /// </summary>
        /// <param name="value">Vector to normalize.</param>
        /// <returns>Normalized copy of the vector.</returns>
        public static Vector3 Normalize(Vector3 value)
        {
            float sqrdLen = value.SqrdLength;
            if (sqrdLen > 1e-04f)
                return value * MathEx.InvSqrt(sqrdLen);

            return value;
        }

        /// <summary>
        /// Calculates the inner product of the two vectors.
        /// </summary>
        /// <param name="lhs">First three dimensional vector.</param>
        /// <param name="rhs">Second three dimensional vector.</param>
        /// <returns>Inner product between the two vectors.</returns>
        public static float Dot(Vector3 lhs, Vector3 rhs)
        {
            return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;
        }

        /// <summary>
        /// Calculates the distance between two points.
        /// </summary>
        /// <param name="a">First three dimensional point.</param>
        /// <param name="b">Second three dimensional point.</param>
        /// <returns>Distance between the two points.</returns>
        public static float Distance(Vector3 a, Vector3 b)
        {
            Vector3 vector3 = new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
            return MathEx.Sqrt(vector3.X * vector3.X + vector3.Y * vector3.Y + vector3.Z * vector3.Z);
        }

        /// <summary>
        /// Scales the components of the vector by specified scale factors.
        /// </summary>
        /// <param name="scale">Scale factors to multiply components by.</param>
        public void Scale(Vector3 scale)
        {
            X *= scale.X;
            Y *= scale.Y;
            Z *= scale.Z;
        }

        /// <summary>
        /// Normalizes the vector.
        /// </summary>
        public void Normalize()
        {
            float sqrdLen = SqrdLength;
            if (sqrdLen > 1e-04f)
                this = this * MathEx.InvSqrt(sqrdLen);
        }

        /// <summary>
        /// Calculates two vectors orthonormal to the first vector.
        /// </summary>
        /// <param name="x">Normalized vector to calculate orthonormal vectors for.</param>
        /// <param name="y">First orthonormal vector.</param>
        /// <param name="z">Second orthonormal vector.</param>
        public static void OrthogonalComplement(Vector3 x, out Vector3 y, out Vector3 z)
        {
            if (MathEx.Abs(x.X) > MathEx.Abs(x.Y))
                y = new Vector3(-x.Z, 0, x.X);
            else
                y = new Vector3(0, x.Z, -x.Y);

            z = Cross(x, y);

            Orthonormalize(ref x, ref y, ref z);
        }

        /// <summary>
        /// Performs Gram-Schmidt orthonormalization on the specified basis, making all the vectors
        /// normal and orthogonal to each other.
        /// </summary>
        /// <param name="x">First vector to orthogonalize.</param>
        /// <param name="y">Second vector to orthogonalize.</param>
        /// <param name="z">Third vector to orthogonalize.</param>
        public static void Orthonormalize(ref Vector3 x, ref Vector3 y, ref Vector3 z)
        {
            x.Normalize();

            float dot0 = Vector3.Dot(x, y);
            y -= dot0 * x;
            y.Normalize();

            float dot1 = Vector3.Dot(y, z);
            dot0 = Vector3.Dot(x, z);
            z -= dot0 * x + dot1 * y;
            z.Normalize();
        }

        /// <summary>
        /// Returns the maximum of all the vector components as a new vector.
        /// </summary>
        /// <param name="a">First vector.</param>
        /// <param name="b">Second vector.</param>
        /// <returns>Vector consisting of maximum components of the first and second vector.</returns>
        public static Vector3 Max(Vector3 a, Vector3 b)
        {
            return new Vector3(MathEx.Max(a.X, b.X), MathEx.Max(a.Y, b.Y), MathEx.Max(a.Z, b.Z));
        }

        /// <summary>
        /// Returns the minimum of all the vector components as a new vector.
        /// </summary>
        /// <param name="a">First vector.</param>
        /// <param name="b">Second vector.</param>
        /// <returns>Vector consisting of minimum components of the first and second vector.</returns>
        public static Vector3 Min(Vector3 a, Vector3 b)
        {
            return new Vector3(MathEx.Min(a.X, b.X), MathEx.Min(a.Y, b.Y), MathEx.Min(a.Z, b.Z));
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return X.GetHashCode() ^ Y.GetHashCode() << 2 ^ Z.GetHashCode() >> 2;
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is Vector3))
                return false;

            Vector3 vec = (Vector3)other;
            if (X.Equals(vec.X) && Y.Equals(vec.Y) && Z.Equals(vec.Z))
                return true;

            return false;
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            return "(" + X + ", " + Y + ", " + Z + ")";
        }
    }

    /** @} */
}
