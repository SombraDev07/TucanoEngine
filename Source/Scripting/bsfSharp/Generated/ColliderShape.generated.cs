//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Represents a single collider shape that can be assigned to a collider.</summary>
	[ShowInInspector]
	public partial class ColliderShape : ScriptObject
	{
		private ColliderShape(bool __dummy0) { }
		protected ColliderShape() { }

		/// <summary>Creates a new plane collider shape.</summary>
		public ColliderShape(PlaneColliderShapeInformation information)
		{
			Internal_CreatePlane(this, ref information);
		}

		/// <summary>Creates a new box collider shape.</summary>
		public ColliderShape(BoxColliderShapeInformation information)
		{
			Internal_CreateBox(this, ref information);
		}

		/// <summary>Creates a new sphere collider shape.</summary>
		public ColliderShape(SphereColliderShapeInformation information)
		{
			Internal_CreateSphere(this, ref information);
		}

		/// <summary>Creates a new capsule collider shape.</summary>
		public ColliderShape(CapsuleColliderShapeInformation information)
		{
			Internal_CreateCapsule(this, ref information);
		}

		/// <summary>Creates a new mesh collider shape.</summary>
		public ColliderShape(MeshColliderShapeInformation information)
		{
			Internal_CreateMesh(this, ref information);
		}

		/// <summary>Returns the type of the collider shape.</summary>
		[NativeWrapper]
		public ColliderShapeType Type
		{
			get { return Internal_GetType(mCachedPtr); }
		}

		/// <summary>Position of the collider shape, relative to the parent collider.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public Vector3 Position
		{
			get
			{
				Vector3 temp;
				Internal_GetPosition(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetPosition(mCachedPtr, ref value); }
		}

		/// <summary>Rotation of the collider shape, relative to the parent collider.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public Quaternion Rotation
		{
			get
			{
				Quaternion temp;
				Internal_GetRotation(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetRotation(mCachedPtr, ref value); }
		}

		/// <summary>Scale of the collider shape, relative to the parent collider.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public Vector3 Scale
		{
			get
			{
				Vector3 temp;
				Internal_GetScale(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetScale(mCachedPtr, ref value); }
		}

		/// <summary>
		/// Enables/disables a collider as a trigger. A trigger will not be used for collisions (objects will pass through it), 
		/// but collision events will still be reported.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public bool IsTrigger
		{
			get { return Internal_GetIsTrigger(mCachedPtr); }
			set { Internal_SetIsTrigger(mCachedPtr, value); }
		}

		/// <summary>
		/// Determines the mass of the collider shape. Only relevant if the parent collider is part of a rigidbody. Ultimately 
		/// this will determine the total mass, center of mass and inertia tensors of the parent rigidbody (if they&apos;re being 
		/// calculated automatically).
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public float Mass
		{
			get { return Internal_GetMass(mCachedPtr); }
			set { Internal_SetMass(mCachedPtr, value); }
		}

		/// <summary>
		/// Determines the physical material of the collider shape. The material determines how objects hitting the collider 
		/// shape.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public RRef<PhysicsMaterial> Material
		{
			get { return Internal_GetMaterial(mCachedPtr); }
			set { Internal_SetMaterial(mCachedPtr, value); }
		}

		/// <summary>
		/// Determines how far apart do two shapes need to be away from each other before the physics runtime starts generating 
		/// repelling impulse for them. This distance will be the sum of contact offsets of the two interacting objects. If 
		/// objects are moving fast you can increase this value to start generating the impulse earlier and potentially prevent 
		/// the objects from interpenetrating. This value is in meters. Must be positive and greater than rest offset.
		///
		/// Also see SetRestOffset().
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public float ContactOffset
		{
			get { return Internal_GetContactOffset(mCachedPtr); }
			set { Internal_SetContactOffset(mCachedPtr, value); }
		}

		/// <summary>
		/// Determines at what distance should two objects resting on one another come to an equilibrium. The value used in the 
		/// runtime will be the sum of rest offsets for both interacting objects. This value is in meters. Cannot be larger than 
		/// contact offset.
		///
		/// Also see SetContactOffset().
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public float RestOffset
		{
			get { return Internal_GetRestOffset(mCachedPtr); }
			set { Internal_SetRestOffset(mCachedPtr, value); }
		}

		/// <summary>
		/// Determines the layer of the collider shape. Layer controls with which shapes will this shape collide.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public ulong Layer
		{
			get { return Internal_GetLayer(mCachedPtr); }
			set { Internal_SetLayer(mCachedPtr, value); }
		}

		/// <summary>Determines which (if any) collision events are reported.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public CollisionReportMode CollisionReportMode
		{
			get { return Internal_GetCollisionReportMode(mCachedPtr); }
			set { Internal_SetCollisionReportMode(mCachedPtr, value); }
		}

		/// <summary>
		/// Returns information about the plane shape. Will return default shape information if the current shape is not a plane.
		/// </summary>
		[NativeWrapper]
		public PlaneColliderShapeInformation PlaneShapeInformation
		{
			get
			{
				PlaneColliderShapeInformation temp;
				Internal_GetPlaneShapeInformation(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>
		/// Returns information about the box shape. Will return default shape information if the current shape is not a box.
		/// </summary>
		[NativeWrapper]
		public BoxColliderShapeInformation BoxShapeInformation
		{
			get
			{
				BoxColliderShapeInformation temp;
				Internal_GetBoxShapeInformation(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>
		/// Returns information about the sphere shape. Will return default shape information if the current shape is not a 
		/// sphere.
		/// </summary>
		[NativeWrapper]
		public SphereColliderShapeInformation SphereShapeInformation
		{
			get
			{
				SphereColliderShapeInformation temp;
				Internal_GetSphereShapeInformation(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>
		/// Returns information about the capsule shape. Will return default shape information if the current shape is not a 
		/// capsule.
		/// </summary>
		[NativeWrapper]
		public CapsuleColliderShapeInformation CapsuleShapeInformation
		{
			get
			{
				CapsuleColliderShapeInformation temp;
				Internal_GetCapsuleShapeInformation(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>
		/// Returns information about the mesh shape. Will return default shape information if the current shape is not a mesh.
		/// </summary>
		[NativeWrapper]
		public MeshColliderShapeInformation MeshShapeInformation
		{
			get
			{
				MeshColliderShapeInformation temp;
				Internal_GetMeshShapeInformation(mCachedPtr, out temp);
				return temp;
			}
		}

		/// <summary>Changes or sets the collider shape to a plane.</summary>
		public void SetShape(PlaneColliderShapeInformation information)
		{
			Internal_SetShape(mCachedPtr, ref information);
		}

		/// <summary>Changes or sets the collider shape to a box.</summary>
		public void SetShape(BoxColliderShapeInformation information)
		{
			Internal_SetShape0(mCachedPtr, ref information);
		}

		/// <summary>Changes or sets the collider shape to a sphere.</summary>
		public void SetShape(SphereColliderShapeInformation information)
		{
			Internal_SetShape1(mCachedPtr, ref information);
		}

		/// <summary>Changes or sets the collider shape to a capsule.</summary>
		public void SetShape(CapsuleColliderShapeInformation information)
		{
			Internal_SetShape2(mCachedPtr, ref information);
		}

		/// <summary>Changes or sets the collider shape to a mesh.</summary>
		public void SetShape(MeshColliderShapeInformation information)
		{
			Internal_SetShape3(mCachedPtr, ref information);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern ColliderShapeType Internal_GetType(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetPosition(IntPtr thisPtr, ref Vector3 position);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetPosition(IntPtr thisPtr, out Vector3 __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetRotation(IntPtr thisPtr, ref Quaternion rotation);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetRotation(IntPtr thisPtr, out Quaternion __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetScale(IntPtr thisPtr, ref Vector3 scale);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetScale(IntPtr thisPtr, out Vector3 __output);
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
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetShape(IntPtr thisPtr, ref PlaneColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetShape0(IntPtr thisPtr, ref BoxColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetShape1(IntPtr thisPtr, ref SphereColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetShape2(IntPtr thisPtr, ref CapsuleColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetShape3(IntPtr thisPtr, ref MeshColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetPlaneShapeInformation(IntPtr thisPtr, out PlaneColliderShapeInformation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetBoxShapeInformation(IntPtr thisPtr, out BoxColliderShapeInformation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetSphereShapeInformation(IntPtr thisPtr, out SphereColliderShapeInformation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetCapsuleShapeInformation(IntPtr thisPtr, out CapsuleColliderShapeInformation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetMeshShapeInformation(IntPtr thisPtr, out MeshColliderShapeInformation __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_CreatePlane(ColliderShape managedInstance, ref PlaneColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_CreateBox(ColliderShape managedInstance, ref BoxColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_CreateSphere(ColliderShape managedInstance, ref SphereColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_CreateCapsule(ColliderShape managedInstance, ref CapsuleColliderShapeInformation information);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_CreateMesh(ColliderShape managedInstance, ref MeshColliderShapeInformation information);
	}
}
