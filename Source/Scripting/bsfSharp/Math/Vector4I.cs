using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Math
     *  @{
     */

    /// <summary>
    /// A four dimensional vector with integer coordinates.
    /// </summary>
    public partial struct Vector4I
    {
        public static readonly Vector4I Zero = new Vector4I(0, 0, 0, 0);
        public static readonly Vector4I One = new Vector4I(1, 1, 1, 1);
        public static readonly Vector4I XAxis = new Vector4I(1, 0, 0, 0);
        public static readonly Vector4I YAxis = new Vector4I(0, 1, 0, 0);
        public static readonly Vector4I ZAxis = new Vector4I(0, 0, 1, 0);

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
                    case 3:
                        return W;
                    default:
                        throw new IndexOutOfRangeException("Invalid Vector4I index.");
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
                    case 3:
                        W = value;
                        break;
                    default:
                        throw new IndexOutOfRangeException("Invalid Vector4I index.");
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
                return (X * X + Y * Y + Z * Z + W * W);
            }
        }

        /// <summary>
        /// Converts a homogenous vector into a three dimensional vector. w component is discarded.
        /// </summary>
        /// <param name="vec">Vector to convert.</param>
        /// <returns>A new three dimensional vector.</returns>
        public static explicit operator Vector3I(Vector4I vec)
        {
            return new Vector3I(vec.X, vec.Y, vec.Z);
        }

        public static Vector4I operator +(Vector4I a, Vector4I b)
        {
            return new Vector4I(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
        }

        public static Vector4I operator -(Vector4I a, Vector4I b)
        {
            return new Vector4I(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
        }

        public static Vector4I operator -(Vector4I v)
        {
            return new Vector4I(-v.X, -v.Y, -v.Z, -v.W);
        }

        public static Vector4I operator *(Vector4I a, Vector4I b)
        {
            return new Vector4I(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
        }

        public static Vector4I operator *(Vector4I v, int d)
        {
            return new Vector4I(v.X * d, v.Y * d, v.Z * d, v.W * d);
        }

        public static Vector4I operator *(int d, Vector4I v)
        {
            return new Vector4I(v.X * d, v.Y * d, v.Z * d, v.W * d);
        }

        public static Vector4I operator /(Vector4I v, int d)
        {
            return new Vector4I(v.X / d, v.Y / d, v.Z / d, v.W / d);
        }

        public static bool operator ==(Vector4I lhs, Vector4I rhs)
        {
            return lhs.X == rhs.X && lhs.Y == rhs.Y && lhs.Z == rhs.Z && lhs.W == rhs.W;
        }

        public static bool operator !=(Vector4I lhs, Vector4I rhs)
        {
            return !(lhs == rhs);
        }

        /// <summary>
        /// Calculates the squared magnitude of the provided vector.
        /// </summary>
        /// <param name="v">Vector to calculate the magnitude for.</param>
        /// <returns>Squared magnitude of the vector.</returns>
        public static int SqrMagnitude(Vector4I v)
        {
            return (v.X * v.X + v.Y * v.Y + v.Z * v.Z + v.W * v.W);
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return X.GetHashCode() ^ Y.GetHashCode() << 2 ^ Z.GetHashCode() >> 2 ^ W.GetHashCode() >> 1;
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is Vector4I))
                return false;

            Vector4I vec = (Vector4I)other;
            if (X.Equals(vec.X) && Y.Equals(vec.Y) && Z.Equals(vec.Z) && W.Equals(vec.W))
                return true;

            return false;
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            return "(" + X + ", " + Y + ", " + Z + ", " + W + ")";
        }
    }
}

