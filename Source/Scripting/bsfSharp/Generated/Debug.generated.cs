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

	/// <summary>Utility class providing various debug functionality.</summary>
	[ShowInInspector]
	public partial class Debug : ScriptObject
	{
		private Debug(bool __dummy0) { }
		protected Debug() { }

		/// <summary>Returns all existing log entries.</summary>
		[NativeWrapper]
		public static LogEntry[] LogEntries
		{
			get { return Internal_GetLogEntries(); }
		}

		/// <summary>Triggered when a new entry in the log is added.</summary>
		public static event Action<LogEntry> OnLogEntryAdded;

		/// <summary>
		/// Triggered whenever one or multiple log entries were added or removed. Triggers only once per frame.
		/// </summary>
		public static event Action OnLogModified;

		/// <summary>Logs a new message.</summary>
		/// <param name="message">The message describing the log entry.</param>
		/// <param name="verbosity">Verbosity of the message, determining its importance.</param>
		/// <param name="categoryName">Category of the message, determining which system is it relevant to.</param>
		public static void Log(string message, LogVerbosity verbosity, string categoryName)
		{
			Internal_Log(message, verbosity, categoryName);
		}

		/// <summary>Removes all log entries for a specific category and/or verbosity level.</summary>
		/// <param name="categoryName">Name of the category to clear. Specify null to clear all categories.</param>
		/// <param name="verbosity">Verbosity level to clear.</param>
		public static void ClearLog(string categoryName, LogVerbosity verbosity = LogVerbosity.Any)
		{
			Internal_ClearLog(categoryName, verbosity);
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_Log(string message, LogVerbosity verbosity, string categoryName);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_ClearLog(string categoryName, LogVerbosity verbosity);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern LogEntry[] Internal_GetLogEntries();
		private static void Internal_OnLogEntryAdded(ref LogEntry p0)
		{
			OnLogEntryAdded?.Invoke(p0);
		}
		private static void Internal_OnLogModified()
		{
			OnLogModified?.Invoke();
		}
	}

	/** @} */
}
