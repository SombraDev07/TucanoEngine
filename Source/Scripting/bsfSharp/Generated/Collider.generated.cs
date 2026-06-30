//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Physics
	 *  @{
	 */

	/// <summary>
	/// Physics object that can be interacted with in the physics world. It can either block other physics objects, or act as 
	/// a trigger. Trigger will report collision events, but won&apos;t actually prevent other physical objects from going 
	/// through them.
	///
	/// This object is intended to remain static in the world. You /can/ move it, but it will not interact with the physics 
	/// world correctly when moved. For that case use Rigidbody instead.
	/// </summary>
	[ShowInInspector]
	public partial class Collider : Component
	{
		private Collider(bool __dummy0) { }
		protected Collider() { }

		/// <summary>
		/// Determines if the collider is a trigger. Trigger collider will not prevent objects from going through its shapes but 
		/// it will still report collision events.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public bool Trigger
		{
			get { return Internal_GetIsTrigger(mCachedPtr); }
			set { Internal_SetIsTrigger(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public float Mass
		{
			get { return Internal_GetMass(mCachedPtr); }
			set { Internal_SetMass(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public RRef<PhysicsMaterial> Material
		{
			get { return Internal_GetMaterial(mCachedPtr); }
			set { Internal_SetMaterial(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public float ContactOffset
		{
			get { return Internal_GetContactOffset(mCachedPtr); }
			set { Internal_SetContactOffset(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public float RestOffset
		{
			get { return Internal_GetRestOffset(mCachedPtr); }
			set { Internal_SetRestOffset(mCachedPtr, value); }
		}

		[ShowInInspector]
		[LayerMask]
		[NativeWrapper]
		public ulong Layer
		{
			get { return Internal_GetLayer(mCachedPtr); }
			set { Internal_SetLayer(mCachedPtr, value); }
		}

		[ShowInInspector]
		[NativeWrapper]
		public CollisionReportMode CollisionReportMode
		{
			get { return Internal_GetCollisionReportMode(mCachedPtr); }
			set { Internal_SetCollisionReportMode(mCachedPtr, value); }
		}

		/// <summary>
		/// Triggered when some object starts interacting with the collider. Only triggered if proper collision report mode is 
		/// turned on.
		/// </summary>
		public event Action<CollisionData> OnCollisionBegin;

		/// <summary>
		/// Triggered for every frame that an object remains interacting with a collider. Only triggered if proper collision 
		/// report mode is turned on.
		/// </summary>
		public event Action<CollisionData> OnCollisionStay;

		/// <summary>
		/// Triggered when some object stops interacting with the collider. Only triggered if proper collision report mode is 
		/// turned on.
		/// </summary>
		public event Action<CollisionData> OnCollisionEnd;

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetIsTrigger(IntPtr thisPtr, bool value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern bool Internal_GetIsTrigger(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetMass(IntPtr thisPtr, float mass);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_GetMass(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetMaterial(IntPtr thisPtr, RRef<PhysicsMaterial> material);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern RRef<PhysicsMaterial> Internal_GetMaterial(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetContactOffset(IntPtr thisPtr, float value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_GetContactOffset(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetRestOffset(IntPtr thisPtr, float value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_GetRestOffset(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetLayer(IntPtr thisPtr, ulong layer);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern ulong Internal_GetLayer(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetCollisionReportMode(IntPtr thisPtr, CollisionReportMode mode);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern CollisionReportMode Internal_GetCollisionReportMode(IntPtr thisPtr);
		private void Internal_OnCollisionBegin(ref CollisionData p0)
		{
			OnCollisionBegin?.Invoke(p0);
		}
		private void Internal_OnCollisionStay(ref CollisionData p0)
		{
			OnCollisionStay?.Invoke(p0);
		}
		private void Internal_OnCollisionEnd(ref CollisionData p0)
		{
			OnCollisionEnd?.Invoke(p0);
		}
	}

	/** @} */
}
