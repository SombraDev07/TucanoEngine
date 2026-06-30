//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Common result codes for TResult/Result types.</summary>
	public enum ResultStatus
	{
		FailedReadOnly = 6,
		Succeeded = 0,
		Failed = 1,
		FailedAlreadyExists = 2,
		FailedInvalidInput = 3,
		FailedInternalError = 4,
		FailedNotFound = 5
	}
}
