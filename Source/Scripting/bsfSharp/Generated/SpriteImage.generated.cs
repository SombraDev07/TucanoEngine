//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Rendering
	 *  @{
	 */

	/// <summary>
	/// Image that references a part of a texture by specifying an UV range. When the sprite image is rendered only the 
	/// portion of the texture specified by the UV range will be rendered.
	///
	/// Sprite images also allow you to specify sprite sheet animation by varying which portion of the UV is selected over 
	/// time.
	/// </summary>
	[ShowInInspector]
	public partial class SpriteImage : Resource
	{
		private SpriteImage(bool __dummy0) { }
		protected SpriteImage() { }

		/// <summary>Returns a reference wrapper for this resource.</summary>
		public RRef<SpriteImage> Ref
		{
			get { return Internal_GetRef(mCachedPtr); }
		}

		/// <summary>
		/// Returns the size of a single animation frame in logical pixel units. If the texture has no animation this is the same 
		/// as GetLogicalSize().
		/// </summary>
		[NativeWrapper]
		public TSize2<int> AnimationFrameSize
		{
			get
			{
				TSize2<int> temp;
				Internal_GetAnimationFrameSize(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>
		/// Sets properties describing sprite animation. The animation splits the sprite area into a grid of sub-images which can 
		/// be evaluated over time. In order to view the animation you must also enable playback through setAnimationPlayback().
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public SpriteSheetGridAnimation Animation
		{
			get
			{
				SpriteSheetGridAnimation temp;
				Internal_GetAnimation(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetAnimation(mCachedPtr, ref value); }
		}

		/// <summary>Determines if and how should the sprite animation play.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public SpriteAnimationPlayback AnimationPlayback
		{
			get { return Internal_GetAnimationPlayback(mCachedPtr); }
			set { Internal_SetAnimationPlayback(mCachedPtr, value); }
		}

		/// <summary>Returns a reference wrapper for this resource.</summary>
		public static implicit operator RRef<SpriteImage>(SpriteImage x)
		{
			if(x != null)
				return Internal_GetRef(x.mCachedPtr);
			else
				return null;
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern RRef<SpriteImage> Internal_GetRef(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetAnimationFrameSize(IntPtr thisPtr, out TSize2<int> __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetAnimation(IntPtr thisPtr, ref SpriteSheetGridAnimation animation);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetAnimation(IntPtr thisPtr, out SpriteSheetGridAnimation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetAnimationPlayback(IntPtr thisPtr, SpriteAnimationPlayback playback);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern SpriteAnimationPlayback Internal_GetAnimationPlayback(IntPtr thisPtr);
	}

	/** @} */
}
