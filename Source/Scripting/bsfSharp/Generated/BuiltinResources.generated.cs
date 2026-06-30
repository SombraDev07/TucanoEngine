//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Holds references to built-in resources used by the core engine.</summary>
	[ShowInInspector]
	public partial class BuiltinResources : ScriptObject
	{
		private BuiltinResources(bool __dummy0) { }
		protected BuiltinResources() { }

		/// <summary>Returns a small entirely white texture.</summary>
		[NativeWrapper]
		public static SpriteTexture WhiteSpriteTexture
		{
			get { return Internal_GetWhiteSpriteTexture(); }
		}

		/// <summary>Returns the default font used by the engine.</summary>
		[NativeWrapper]
		public static Font DefaultFont
		{
			get { return Internal_GetDefaultFont(); }
		}

		/// <summary>Returns one of the builtin shader types.</summary>
		public static Shader GetBuiltinShader(BuiltinShader type)
		{
			return Internal_GetBuiltinShader(type);
		}

		/// <summary>Retrieves one of the builtin meshes.</summary>
		public static Mesh GetMesh(BuiltinMesh mesh)
		{
			return Internal_GetMesh(mesh);
		}

		/// <summary>Loads a shader at the specified path.</summary>
		/// <param name="path">Path relative to the default shader folder with no file extension.</param>
		public static Shader GetShader(string path)
		{
			return Internal_GetShader(path);
		}

		/// <summary>
		/// Attempts to return a font of the given font family. Returns the default font is provided font is not found.
		/// </summary>
		public static Font GetFont(string font)
		{
			return Internal_GetFont(font);
		}

		/// <summary>
		/// Loads the shader with the specified name from the cache if available, or compiles the shader from source if not 
		/// available.
		/// </summary>
		/// <param name="path">Absolute path to the shader source file.</param>
		/// <returns>Valid shader if successful, or null otherwise.</returns>
		public static Shader GetOrCompileShader(string path)
		{
			return Internal_GetOrCompileShader(path);
		}

		/// <summary>Retrieves one of the builtin textures.</summary>
		public static RRef<Texture> GetTexture(BuiltinTexture type)
		{
			return Internal_GetTexture(type);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern SpriteTexture Internal_GetWhiteSpriteTexture();
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Shader Internal_GetBuiltinShader(BuiltinShader type);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Mesh Internal_GetMesh(BuiltinMesh mesh);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Shader Internal_GetShader(string path);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Font Internal_GetFont(string font);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Shader Internal_GetOrCompileShader(string path);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Font Internal_GetDefaultFont();
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern RRef<Texture> Internal_GetTexture(BuiltinTexture type);
	}
}
