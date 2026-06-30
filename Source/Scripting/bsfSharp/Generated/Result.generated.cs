//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/// <summary>Same as TResult, but with no output object.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct Result
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static Result Default()
		{
			Result value = new Result();
			value.Status = ResultStatus.Failed;
			value.ErrorMessage = "";
			value.AdditionalErrorMessage = "";

			return value;
		}

		public ResultStatus Status;
		public string ErrorMessage;
		public string AdditionalErrorMessage;
	}
}
