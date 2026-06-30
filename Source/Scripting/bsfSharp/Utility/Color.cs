//********************************* B3D Framework - Copyright 2018-2019 Marko Pintera ************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.InteropServices;

namespace b3d
{
    /** @addtogroup Utility
     *  @{
     */

    /// <summary>
    /// Contains a three-component color with an alpha component.
    /// </summary>
    public partial struct Color
    {
        public static Color Red { get { return new Color(1.0f, 0.0f, 0.0f, 1.0f); } }
        public static Color Green { get { return new Color(0.0f, 1.0f, 0.0f, 1.0f); } }
        public static Color Blue { get { return new Color(0.0f, 0.0f, 1.0f, 1.0f); } }
        public static Color Yellow { get { return new Color(1.0f, 1.0f, 0.0f, 1.0f); } }
        public static Color Cyan { get { return new Color(0.0f, 1.0f, 1.0f, 1.0f); } }
        public static Color Magenta { get { return new Color(1.0f, 0.0f, 1.0f, 1.0f); } }
        public static Color White { get { return new Color(1.0f, 1.0f, 1.0f, 1.0f); } }
        public static Color Black { get { return new Color(0.0f, 0.0f, 0.0f, 1.0f); } }
        public static Color DarkCyan { get { return new Color(0.0f, (114.0f / 255.0f), (188.0f / 255.0f), 1.0f); } }
        public static Color VeryDarkGray { get { return new Color(23.0f / 255.0f, 23.0f / 255.0f, 23.0f / 255.0f, 1.0f); } }
        public static Color DarkGray { get { return new Color(63.0f / 255.0f, 63.0f / 255.0f, 63.0f / 255.0f, 1.0f); } }
        public static Color LightGray { get { return new Color(200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f); } }
        public static Color BansheeOrange { get { return new Color(1.0f, (168.0f/255.0f), 0.0f, 1.0f); } }
        public static Color Transparent { get { return new Color(0.0f, 0.0f, 0.0f, 0.0f); } }

        /// <summary>
        /// Accesses color components by an index.
        /// </summary>
        /// <param name="index">Index ranging [0, 3]</param>
        /// <returns>Value of the component at the specified index.</returns>
        public float this[int index]
        {
            get
            {
                switch (index)
                {
                    case 0:
                        return R;
                    case 1:
                        return G;
                    case 2:
                        return B;
                    case 3:
                        return A;
                    default:
                        throw new IndexOutOfRangeException("Invalid Color index.");
                }
            }
            set
            {
                switch (index)
                {
                    case 0:
                        R = value;
                        break;
                    case 1:
                        G = value;
                        break;
                    case 2:
                        B = value;
                        break;
                    case 3:
                        A = value;
                        break;
                    default:
                        throw new IndexOutOfRangeException("Invalid Color index.");
                }
            }
        }

        /// <summary>
        /// Creates a new default initialized color value.
        /// </summary>
        public static Color Default()
        {
            return new Color();
        }

        public static Color operator+ (Color a, Color b)
        {
            return new Color(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
        }

        public static Color operator- (Color a, Color b)
        {
            return new Color(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
        }

        public static Color operator* (Color a, Color b)
        {
            return new Color(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A);
        }

        public static Color operator* (Color a, float b)
        {
            return new Color(a.R * b, a.G * b, a.B * b, a.A * b);
        }

        public static Color operator* (float b, Color a)
        {
            return new Color(a.R * b, a.G * b, a.B * b, a.A * b);
        }

        public static Color operator/ (Color a, float b)
        {
            return new Color(a.R / b, a.G / b, a.B / b, a.A / b);
        }

        public static bool operator ==(Color lhs, Color rhs)
        {
            return lhs.R == rhs.R && lhs.G == rhs.G && lhs.B == rhs.B && lhs.A == rhs.A;
        }

        public static bool operator !=(Color lhs, Color rhs)
        {
            return !(lhs == rhs);
        }
        
        /// <summary>
        /// Converts the current color from gamma to linear space and returns the result.
        /// </summary>
        public Color Linear =>
            new Color(
                SRGBToLinear(R),
                SRGBToLinear(G),
                SRGBToLinear(B),
                A);

        /// <summary>
        /// Converts the current color from linear to gamma space and returns the result.
        /// </summary>
        public Color Gamma =>
            new Color(
                LinearToSRGB(R),
                LinearToSRGB(G),
                LinearToSRGB(B),
                A);

        /// <summary>
        /// Converts the provided RGB color into HSV color space.
        /// </summary>
        /// <param name="input">Color in RGB color space.</param>
        /// <returns>Color in HSV color space.</returns>
        public static Color RGB2HSV(Color input)
        {
            Color output = input;

            float min = input.R < input.G ? input.R : input.G;
            min = min < input.B ? min : input.B;

            float max = input.R > input.G ? input.R : input.G;
            max = max > input.B ? max : input.B;

            output.B = max;

            if (max == 0.0f)
            {
                output.R = 0.0f;
                output.G = 0.0f;

                return output;
            }

            float delta = max - min;
            if (delta != 0.0f)
            {
                output.G = (delta / max);
            }
            else
            {
                output.G = 0.0f;
                delta = 1.0f;
            }

            if (input.R >= max)
                output.R = (input.G - input.B) / delta;
            else
            {
                if (input.G >= max)
                    output.R = 2.0f + (input.B - input.R) / delta;
                else
                    output.R = 4.0f + (input.R - input.G) / delta;
            }

            output.R /= 6.0f;

            if (output.R < 0.0f)
                output.R += 1.0f;

            return output;
        }

        /// <summary>
        /// Converts the provided HSV color into RGB color space.
        /// </summary>
        /// <param name="input">Color in HSV color space.</param>
        /// <returns>Color in RGB color space.</returns>
        public static Color HSV2RGB(Color input)
        {
            Color output = input;

            if (input.G <= 0.0)
            {
                output.R = input.B;
                output.G = input.B;
                output.B = input.B;

                return output;
            }

            float hh = input.R;
            if (hh >= 1.0f)
                hh = 0.0f;

            hh *= 6.0f;

            int i = (int)hh;
            float ff = hh - i;
            float p = input.B * (1.0f - input.G);
            float q = input.B * (1.0f - (input.G * ff));
            float t = input.B * (1.0f - (input.G * (1.0f - ff)));

            switch (i)
            {
                case 0:
                    output.R = input.B;
                    output.G = t;
                    output.B = p;
                    break;
                case 1:
                    output.R = q;
                    output.G = input.B;
                    output.B = p;
                    break;
                case 2:
                    output.R = p;
                    output.G = input.B;
                    output.B = t;
                    break;
                case 3:
                    output.R = p;
                    output.G = q;
                    output.B = input.B;
                    break;
                case 4:
                    output.R = t;
                    output.G = p;
                    output.B = input.B;
                    break;
                default:
                    output.R = input.B;
                    output.G = p;
                    output.B = q;
                    break;
            }

            return output;
        }

        /// <inheritdoc/>
        public override int GetHashCode()
        {
            return R.GetHashCode() ^ G.GetHashCode() << 2 ^ B.GetHashCode() >> 2 ^ A.GetHashCode() >> 1;
        }

        /// <inheritdoc/>
        public override bool Equals(object other)
        {
            if (!(other is Color))
                return false;

            Color color = (Color)other;
            if (R.Equals(color.R) && G.Equals(color.G) && B.Equals(color.B) && A.Equals(color.A))
                return true;

            return false;
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            return "(" + R + ", " + G + ", " + B + ", " + A + ")";
        }
        
        
        /// <summary>
        /// Converts a single color component from linear to SRGB (gamma) space.
        /// </summary>
        /// <param name="x">Component in linear space.</param>
        /// <returns>Component in SRGB space.</returns>
        private static float LinearToSRGB(float x)
        {
            if (x <= 0.0f)
                return 0.0f;
            else if (x >= 1.0f)
                return 1.0f;
            else if (x < 0.0031308f)
                return x * 12.92f;
            else
                return MathEx.Pow(x, 1.0f / 2.4f) * 1.055f - 0.055f;
        }

        /// <summary>
        /// Converts a single color component from SRGB (gamma) to linear space.
        /// </summary>
        /// <param name="x">Component in SRGB space.</param>
        /// <returns>Component in linear space.</returns>
        private static float SRGBToLinear(float x)
        {
            if (x <= 0.0f)
                return 0.0f;
            else if (x >= 1.0f)
                return 1.0f;
            else if (x < 0.04045f)
                return x / 12.92f;
            else
                return MathEx.Pow((x + 0.055f) / 1.055f, 2.4f);
        }
    }

    /** @} */
}
