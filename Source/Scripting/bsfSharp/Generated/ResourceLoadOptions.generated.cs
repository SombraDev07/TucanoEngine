//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
#if IS_B3D
	/// <summary>Options that may be used to customize resource load operation.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct ResourceLoadOptions
	{
		public ResourceLoadOptions(bool asynchronousLoad = true, bool loadDependencies = true, bool keepInternalReference = true)
		{
			this.AsynchronousLoad = asynchronousLoad;
			this.LoadDependencies = loadDependencies;
			this.KeepInternalReference = keepInternalReference;
		}

		public bool AsynchronousLoad;
		public bool LoadDependencies;
		public bool KeepInternalReference;
	}
#endif
}
