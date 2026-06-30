//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>
	/// Keeps track of all active SceneObject%s and their components. Keeps track of component state and triggers their 
	/// events. Updates the transforms of objects as SceneObject%s move.
	/// </summary>
	[ShowInInspector]
	public partial class SceneManager : ScriptObject
	{
		private SceneManager(bool __dummy0) { }
		protected SceneManager() { }

		/// <summary>
		/// In a standalone game this represents the scene that is playing. In editor this represents the primary scene being 
		/// edited. When creating scene objects and no scene is provided, the object will be created in the main scene. If null 
		/// main scene is set, new empty main scene will be created internally, as the main scene must always exist.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public static SceneInstance MainScene
		{
			get { return Internal_GetMainScene(); }
			set { Internal_SetMainScene(value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetMainScene(SceneInstance scene);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern SceneInstance Internal_GetMainScene();
	}
}
