//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace b3d
{
	/** @addtogroup Debug
	 *  @{
	 */

	/// <summary>A single log entry, containing a message and a channel the message was recorded on.</summary>
	[StructLayout(LayoutKind.Sequential), SerializeObject]
	public partial struct LogEntry
	{
		/// <summary>Initializes the struct with default values.</summary>
		public static LogEntry Default()
		{
			LogEntry value = new LogEntry();
			value.Message = "";
			value.Verbosity = LogVerbosity.Info;
			value.CategoryName = "";
			value.LocalTime = 0;

			return value;
		}

		public string Message;
		public LogVerbosity Verbosity;
		public string CategoryName;
		/// <summary>Local time of message</summary>
		public ulong LocalTime;
	}

	/** @} */
}
