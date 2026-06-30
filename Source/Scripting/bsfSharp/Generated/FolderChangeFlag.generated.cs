//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Types of notifications we would like to receive when we start a FolderMonitor on a certain folder.</summary>
	public enum FolderChangeFlag
	{
		/// <summary>Called when file is created, moved or removed.</summary>
		FileAddedOrRemoved = 1,
		/// <summary>Called when directory is created, moved or removed.</summary>
		DirectoryAddedOrRemoved = 2,
		/// <summary>Called when file is written to.</summary>
		FileWritten = 4
	}
}
