//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DUtilityPrerequisites.h"
#include "Private/Unix/B3DUnixFileDataStream.h"
#include "Threading/B3DThreading.h"

// libdispatch is a C API; treat dispatch objects as plain C pointers managed manually with dispatch_retain/release.
// This must be defined before <dispatch/dispatch.h> is included anywhere in the TU. In a .cpp TU __has_feature(objc_arc)
// is always false, so OS_OBJECT_USE_OBJC would default to 0 here anyway - the explicit define makes the assumption load-
// bearing rather than incidental. The #error below prevents this header from being included from an ARC-enabled Obj-C++
// TU, where the macro would conflict with ARC's lifetime management of dispatch objects.
#define OS_OBJECT_USE_OBJC 0
#include <dispatch/dispatch.h>
#if OS_OBJECT_USE_OBJC
#	error "B3DMacOSFileDataStream.h must not be included from ARC-enabled Obj-C(++) translation units."
#endif

namespace b3d
{
	/** @addtogroup Filesystem
	 *  @{
	 */

	/**
	 * macOS-specific file data stream. Inherits the synchronous POSIX surface from UnixFileDataStream and adds a native
	 * asynchronous ReadAsync() backed by Grand Central Dispatch's dispatch_io channel when FileAccessFlag::Async is set.
	 *
	 * When opened without Async, no async machinery is initialized and ReadAsync() falls back to the synchronous default
	 * inherited from DataStream. When opened with Async, Open() creates a DISPATCH_IO_RANDOM channel over the file
	 * descriptor; completion handlers run on the process-wide QOS_CLASS_UTILITY global concurrent queue (no per-stream
	 * queue is created). If initialization fails, the stream stays usable and ReadAsync() degrades to the synchronous
	 * fallback.
	 */
	class MacOSFileDataStream final : public UnixFileDataStream
	{
	public:
		MacOSFileDataStream(const Path& filePath, FileAccessFlags access = FileAccessFlag::Read);
		~MacOSFileDataStream() override;

		/** Opens the file stream and (if Async was requested) initializes the dispatch_io async backend. */
		bool Open() override;
		TAsyncOp<TShared<MemoryDataStream>> ReadAsync(u64 offset, size_t byteCount, TOptional<DataRange> userSuppliedMemory = TOptional<DataRange>()) override;
		TShared<DataStream> Clone(bool copyData = true) const override;
		bool Close() override;

	private:
		/** State for a single in-flight asynchronous read. Lives on the heap from issue through finalization. */
		struct AsyncReadRequest;

		/** Builds the result, completes the operation, releases the request and decrements the outstanding read count. */
		void FinalizeAsyncRead(AsyncReadRequest* request, size_t bytesRead, bool succeeded);

		// Async backend state - only initialized and used when mAccess includes FileAccessFlag::Async and the backend successfully came up.
		dispatch_io_t mChannel = nullptr;
		Mutex mAsyncMutex;
		ConditionVariable mAllReadsComplete;
		u32 mOutstandingReads = 0;
		bool mAsyncReady = false;
		bool mClosed = false;
	};

	/** @} */
} // namespace b3d
