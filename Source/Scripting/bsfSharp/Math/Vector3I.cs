using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    /// <summary>
    /// A three dimensional vector with integer coordinates.
    /// </summary>
    public partial struct Vector3I
    {
        public static readonly Vector3I Zero = new Vector3I(0, 0, 0);
        public static readonly Vector3I One = new Vector3I(1, 1, 1);
        public static readonly Vector3I XAxis = new Vector3I(1, 0, 0);
        public static readonly Vector3I YAxis = new Vector3I(0, 1, 0);
        public static readonly Vector3I ZAxis = new Vector3I(0, 0, 1);

        /// <summary>
        /// Accesses a specific component of the vector.
        /// </summary>
        /// <param name="index">Index of the component.</param>
        /// <returns>Value of the specific component.</returns>
        public int this[int index]
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
                        throw new IndexOutOfRangeException("Invalid Vector3I index.");
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
                        throw new IndexOutOfRangeException("Invalid Vector3I index.");
                }
            }
        }

        /// <summary>
        /// Returns the squared length of the vector.
        /// </summary>
        public int SqrdLength
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
        public static explicit operator Vector4I(Vector3I vec)
        {
            return new Vector4I(vec.X, vec.Y, vec.Z, 0);
        }

        public static Vector3I operator +(Vector3I a, Vector3I b)
        {
            return new Vector3I(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        public static Vector3I operator -(Vector3I a, Vector3I b)
        {
            return new Vector3I(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        public static Vector3I operator -(Vector3I v)
        {
            return new Vector3I(-v.X, -v.Y, -v.Z);
        }

        public static Vector3I operator *(Vector3I a, Vector3I b)
        {
            return new Vector3I(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
        }

        public static Vector3I operator *(Vector3I v, int d)
        {
            return new Vector3I(v.X * d, v.Y * d, v.Z * d);
        }

        public static Vector3I operator *(int d, Vector3I v)
        {
            return new Vector3I(v.X * d, v.Y * d, v.Z * d);
        }

        public static Vector3I operator /(Vector3I v, int d)
        {
            return new Vector3I(v.X / d, v.Y / d, v.Z / d);
        }

        public static bool operator ==(Vector3I lhs, Vector3I rhs)
        {
            return lhs.X == rhs.X && lhs.Y == rhs.Y && lhs.Z == rhs.Z;
        }

        public static bool operator !=(Vector3I lhs, Vector3I rhs)
        {
            return !(lhs == rhs);
        }

        /// <summary>
        /// Calculates the squared magnitude of the provided vector.
        /// </summary>
        /// <param name="v">Vector to calculate the magnitude for.</param>
        /// <returns>Squared magnitude of the vector.</returns>
        public static int SqrMagnitude(Vector3I v)
        {
            return (v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        }

        /// <summary>
        /// Returns the maximum of all the vector components as a new vector.
        /// </summary>
        /// <param name="a">First vector.</param>
        /// <param name="b">Second vector.</param>
        /// <returns>Vector consisting of maximum components of the first and second vector.</returns>
        public static Vector3I Max(Vector3I a, Vector3I b)
        {
            return new Vector3I(MathEx.Max(a.X, b.X), MathEx.Max(a.Y, b.Y), MathEx.Max(a.Z, b.Z));
        }

        /// <summary>
        /// Returns the minimum of all the vector components as a new vector.
        /// </summary>
        /// <param name="a">First vector.</param>
        /// <param name="b">Second vector.</param>
        /// <returns>Vector consisting of minimum components of the first and second vector.</returns>
        public static Vector3I Min(Vector3I a, Vector3I b)
        {
            return new Vector3I(MathEx.Min(a.X, b.X), MathEx.Min(a.Y, b.Y), MathEx.Min(a.Z, b.Z));
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return X.GetHashCode() ^ Y.GetHashCode() << 2 ^ Z.GetHashCode() >> 2;
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is Vector3I))
                return false;

            Vector3I vec = (Vector3I)other;
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
}
