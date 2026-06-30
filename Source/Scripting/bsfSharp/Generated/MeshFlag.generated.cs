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

	/// <summary>Flags that control Mesh behaviour.</summary>
	public enum MeshFlag
	{
		/// <summary>
		/// Mesh is not going to change ever, or is going to change rarely. Mesh vertex &amp; index buffers will be allocated in 
		/// GPU memory.
		/// </summary>
		Static = 1,
		/// <summary>
		/// Mesh is going to change often (e.g. every frame). Mesh vertex &amp; index buffers will be allocated in CPU memory 
		/// which is GPU accessible.
		/// </summary>
		Dynamic = 2,
		/// <summary>
		/// Mesh vertex and input buffers can be bound for unordered access (i.e. structured storage buffers) in the shaders. 
		/// Provide this your shader is manually pulling vertex/index data in the shader.
		/// </summary>
		UnorderedAccess = 4,
		/// <summary>
		/// Normally when a mesh is uploaded to the GPU, the CPU memory is no longer needed so it will be released. If this flag 
		/// is provided the CPU mesh data will be kept. This allows you to access mesh data on the CPU.
		/// </summary>
		KeepCPUCopy = 8
	}

	/** @} */
}
