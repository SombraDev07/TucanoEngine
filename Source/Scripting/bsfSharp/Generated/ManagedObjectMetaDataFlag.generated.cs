//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Flags that are used to further desribe a type of a managed serializable object.</summary>
	public enum ManagedObjectMetaDataFlag
	{
		Serializable = 1,
		Inspectable = 2
	}
}
