//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup GUI
	 *  @{
	 */

	/// <summary>
	/// A GUI element that allows the user to draw custom graphics. All drawn elements relative to the canvas, to its origin 
	/// in the top left corner.
	/// </summary>
	[ShowInInspector]
	public partial class GUICanvas : GUIInteractable
	{
		private GUICanvas(bool __dummy0) { }
		protected GUICanvas() { }

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="styleClass">
		/// Style class that will be used for determining GUI element visuals from the current style sheet. If no class is 
		/// provided, default style is determined based on GUI element type.
		/// </param>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUICanvas(string styleClass, params GUIOption[] options)
		{
			Internal_Create(this, styleClass, options);
		}

		/// <summary>Creates a new GUI element.</summary>
		/// <param name="options">
		/// Additional options that control GUI element size and position. This will override options set in the style sheet.
		/// </param>
		public GUICanvas(params GUIOption[] options)
		{
			Internal_Create0(this, options);
		}

		/// <summary>Draws a line going from <paramref name="a"/> to <paramref name="b"/>.</summary>
		/// <param name="a">Starting point of the line, relative to the canvas origin (top-left).</param>
		/// <param name="b">Ending point of the line, relative to the canvas origin (top-left).</param>
		/// <param name="color">Color of the line.</param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawLine(TVector2<TUnitValue<int,LogicalPixel>> a, TVector2<TUnitValue<int,LogicalPixel>> b, Color color, byte depth = 128)
		{
			Internal_DrawLine(mCachedPtr, ref a, ref b, ref color, depth);
		}

		/// <summary>
		/// Draws multiple lines following the path by the provided vertices. First vertex connects to the second vertex, and 
		/// every following vertex connects to the previous vertex.
		/// </summary>
		/// <param name="vertices">
		/// Points to use for drawing the line. Must have at least two elements. All points are relative to the canvas origin 
		/// (top-left).
		/// </param>
		/// <param name="color">Color of the line.</param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawPolyLine(TVector2<TUnitValue<int,LogicalPixel>>[] vertices, Color color, byte depth = 128)
		{
			Internal_DrawPolyLine(mCachedPtr, vertices, ref color, depth);
		}

		/// <summary>Draws a quad with a the provided image displayed.</summary>
		/// <param name="image">Image to draw.</param>
		/// <param name="area">
		/// Position and size of the texture to draw. Position is relative to the canvas origin (top-left). If size is zero, the 
		/// default texture size will be used.
		/// </param>
		/// <param name="color">Color to tint the drawn texture with.</param>
		/// <param name="scaleMode">
		/// Scale mode to use when sizing the texture. Only relevant if the provided quad size doesn&apos;t match the texture 
		/// size.
		/// </param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawImage(RRef<SpriteImage> image, TArea2<TUnitValue<int,LogicalPixel>,TUnitValue<int,LogicalPixel>> area, Color color, TextureScaleMode scaleMode = TextureScaleMode.StretchToFit, byte depth = 128)
		{
			Internal_DrawImage(mCachedPtr, image, ref area, ref color, scaleMode, depth);
		}

		/// <summary>
		/// Draws a triangle strip. First three vertices are used to form the initial triangle, and every next vertex will form a 
		/// triangle with the previous two.
		/// </summary>
		/// <param name="vertices">
		/// A set of points defining the triangles. Must have at least three elements. All points are relative to the canvas 
		/// origin (top-left).
		/// </param>
		/// <param name="color">Color of the triangles.</param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawTriangleStrip(TVector2<TUnitValue<int,LogicalPixel>>[] vertices, Color color, byte depth = 128)
		{
			Internal_DrawTriangleStrip(mCachedPtr, vertices, ref color, depth);
		}

		/// <summary>Draws a triangle list. Every three vertices in the list represent a unique triangle.</summary>
		/// <param name="vertices">
		/// A set of points defining the triangles. Must have at least three elements, and its size must be a multiple of three.
		/// </param>
		/// <param name="color">Color of the triangles.</param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawTriangleList(TVector2<TUnitValue<int,LogicalPixel>>[] vertices, Color color, byte depth = 128)
		{
			Internal_DrawTriangleList(mCachedPtr, vertices, ref color, depth);
		}

		/// <summary>
		/// Draws a piece of text with the wanted font. The text will be aligned to the top-left corner of the provided position, 
		/// and will not be word wrapped.
		/// </summary>
		/// <param name="text">Text to draw.</param>
		/// <param name="position">
		/// Position of the text to draw. This represents the top-left corner of the text. It is relative to the canvas origin 
		/// (top-left).
		/// </param>
		/// <param name="font">Font to draw the text with.</param>
		/// <param name="size">Size of the font.</param>
		/// <param name="color">Color of the text.</param>
		/// <param name="depth">
		/// Depth at which to draw the element. Elements with higher depth will be drawn before others. Additionally elements of 
		/// the same type (triangle or line) will be drawn in order they are submitted if they share the same depth.
		/// </param>
		public void DrawText(string text, TVector2<TUnitValue<int,LogicalPixel>> position, RRef<Font> font, float size, Color color, byte depth = 128)
		{
			Internal_DrawText(mCachedPtr, text, ref position, font, size, ref color, depth);
		}

		/// <summary>Clears the canvas, removing any previously drawn elements.</summary>
		public void Clear()
		{
			Internal_Clear(mCachedPtr);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawLine(IntPtr thisPtr, ref TVector2<TUnitValue<int,LogicalPixel>> a, ref TVector2<TUnitValue<int,LogicalPixel>> b, ref Color color, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawPolyLine(IntPtr thisPtr, TVector2<TUnitValue<int,LogicalPixel>>[] vertices, ref Color color, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawImage(IntPtr thisPtr, RRef<SpriteImage> image, ref TArea2<TUnitValue<int,LogicalPixel>,TUnitValue<int,LogicalPixel>> area, ref Color color, TextureScaleMode scaleMode, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawTriangleStrip(IntPtr thisPtr, TVector2<TUnitValue<int,LogicalPixel>>[] vertices, ref Color color, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawTriangleList(IntPtr thisPtr, TVector2<TUnitValue<int,LogicalPixel>>[] vertices, ref Color color, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_DrawText(IntPtr thisPtr, string text, ref TVector2<TUnitValue<int,LogicalPixel>> position, RRef<Font> font, float size, ref Color color, byte depth);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Clear(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create(GUICanvas managedInstance, string styleClass, params GUIOption[] options);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Create0(GUICanvas managedInstance, params GUIOption[] options);
	}

	/** @} */
}
