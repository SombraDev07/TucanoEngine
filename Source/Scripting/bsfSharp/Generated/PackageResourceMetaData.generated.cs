//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Contains meta-data for a resource stored in a Package.</summary>
	[ShowInInspector]
	public partial class PackageResourceMetaData : ScriptObject
	{
		private PackageResourceMetaData(bool __dummy0) { }
		protected PackageResourceMetaData() { }

		/// <summary>Returns the name of the resource.</summary>
		[NativeWrapper]
		public string ResourceName
		{
			get { return Internal_GetResourceName(mCachedPtr); }
		}

		/// <summary>Path to the resource within the package.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public string Path
		{
			get { return Internal_GetPath(mCachedPtr); }
			set { Internal_SetPath(mCachedPtr, value); }
		}

		/// <summary>Unique ID of the resource.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public UUID Id
		{
			get
			{
				UUID temp;
				Internal_GetId(mCachedPtr, out temp);
				return temp;
			}
			set { Internal_SetId(mCachedPtr, ref value); }
		}

		/// <summary>RTTI type ID of the resource contained.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public int TypeId
		{
			get { return Internal_GetTypeId(mCachedPtr); }
			set { Internal_SetTypeId(mCachedPtr, value); }
		}

		/// <summary>IDs of other resource that this resource depends on.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public UUID[] Dependencies
		{
			get { return Internal_GetDependencies(mCachedPtr); }
			set { Internal_SetDependencies(mCachedPtr, value); }
		}

		/// <summary>Type of compression used on the serialized resource data.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public CompressionType CompressionType
		{
			get { return Internal_GetCompressionType(mCachedPtr); }
			set { Internal_SetCompressionType(mCachedPtr, value); }
		}

		/// <summary>Flags to provide additional information about the resource.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public PackageResourceFlag Flags
		{
			get { return Internal_GetFlags(mCachedPtr); }
			set { Internal_SetFlags(mCachedPtr, value); }
		}

		/// <summary>
		/// Optional additional meta-data set explicitly by the user. This can be anything, but should be kept small.
		/// </summary>
		[ShowInInspector]
		[NativeWrapper]
		public PackageResourceUserMetaData AdditionalMetaData
		{
			get { return Internal_GetAdditionalMetaData(mCachedPtr); }
			set { Internal_SetAdditionalMetaData(mCachedPtr, value); }
		}

		/// <summary>Meta-data that is inherited from the Resource object.</summary>
		[ShowInInspector]
		[NativeWrapper]
		public ResourceMetaData ResourceMetaData
		{
			get { return Internal_GetResourceMetaData(mCachedPtr); }
			set { Internal_SetResourceMetaData(mCachedPtr, value); }
		}

		/// <summary>Returns managed type of the resource described by the meta-data.</summary>
		[NativeWrapper]
		public Type ResourceType
		{
			get { return Internal_GetResourceType(mCachedPtr); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string Internal_GetResourceName(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern string Internal_GetPath(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetPath(IntPtr thisPtr, string value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_GetId(IntPtr thisPtr, out UUID __output);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetId(IntPtr thisPtr, ref UUID value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern int Internal_GetTypeId(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetTypeId(IntPtr thisPtr, int value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern UUID[] Internal_GetDependencies(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetDependencies(IntPtr thisPtr, UUID[] value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern CompressionType Internal_GetCompressionType(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetCompressionType(IntPtr thisPtr, CompressionType value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern PackageResourceFlag Internal_GetFlags(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetFlags(IntPtr thisPtr, PackageResourceFlag value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern PackageResourceUserMetaData Internal_GetAdditionalMetaData(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetAdditionalMetaData(IntPtr thisPtr, PackageResourceUserMetaData value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern ResourceMetaData Internal_GetResourceMetaData(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_SetResourceMetaData(IntPtr thisPtr, ResourceMetaData value);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Type Internal_GetResourceType(IntPtr thisPtr);
	}
}
