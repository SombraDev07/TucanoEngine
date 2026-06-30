//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DUtilityPrerequisites.h"
#include "Private/Unix/B3DUnixFileDataStream.h"
#include "Threading/B3DThreading.h"
#include "Utility/B3DModule.h"
#include "Utility/B3DPool.h"

#include <atomic>
#include <liburing.h>

namespace b3d
{
	/** @addtogroup Filesystem
	 *  @{
	 */

	class LinuxFileDataStream;

	/**
	 * Process-wide owner of the shared io_uring instance that backs every LinuxFileDataStream's async reads.
	 *
	 * Threading contract:
	 *   - The reaper thread is the single owner of the ring. Only the reaper touches the submission & completion queues (SQE/CQE).
	 *   - RequestRead() is safe from any thread. The reaper drains the queue on its next wake and issues the SQEs itself.
	 *   - The reaper signals completion by calling LinuxFileDataStream::OnAsyncReadComplete() on the owning stream,
	 *     which the stream uses to release its outstanding-read count and wake any Close()-side drain.
	 */
	class LinuxFileIOManager final : public Module<LinuxFileIOManager>
	{
	public:
		/** Describes a single async read. The owning stream fills it in and passes it to RequestRead(). */
		struct AsyncReadRequest
		{
			LinuxFileDataStream* Stream = nullptr; /**< Owning stream; receives OnAsyncReadComplete() after finalize. */
			int FileDescriptor = -1; /**< Descriptor to read from; the stream keeps it alive for the operation's lifetime. */
			void* Buffer = nullptr; /**< Destination the read writes into. */
			bool OwnsBuffer = false; /**< True if the manager should free Buffer (via B3DFree) when the read fails. */
			u64 BaseOffset = 0; /**< File offset where the read begins. */
			u64 TotalByteCount = 0; /**< Total bytes to read (already clamped to EOF by the caller). */
			TAsyncOp<TShared<MemoryDataStream>> Op; /**< Completed with the read result, or a null stream on failure. */
		};

		/** True iff OnStartUp() successfully brought the ring online and the reaper is reaping. */
		bool IsRunning() const { return mRunning.load(std::memory_order_acquire); }

		/**
		 * Queues an async read with the shared ring. The manager issues the read (chunked across CQEs as needed),
		 * finalizes it, completes the AsyncOp, and notifies the owning stream when it finishes (or fails).
		 * Safe from any thread.
		 *
		 * If the manager is not running (e.g. shutting down), the read is finalized inline as a failure - the stream's
		 * OnAsyncReadComplete() still runs so the outstanding-read counter is consistent.
		 */
		void RequestRead(const AsyncReadRequest& request);

	protected:
		/** Brings up the ring + eventfd + reaper thread. Logs Warning and leaves IsRunning() == false on any failure. */
		void OnStartUp() override;

		/** Signals the reaper to stop, waits for it to exit, tears down the ring + eventfd. */
		void OnShutDown() override;

	private:
		/**
		 * One in-flight async read: the caller's request plus the reaper-internal chunk accumulator. Pooled in
		 * mOperationPool, so its address (used as the SQE user_data) stays stable for the operation's whole lifetime.
		 */
		struct AsyncReadOperation
		{
			AsyncReadRequest Request;
			u64 BytesRead = 0; /**< Bytes accumulated so far across all chunks. */
		};

		/** Reaper thread entry point: waits on the ring, reaps CQEs, drains the pending queue, chains chunks. */
		void ReaperThreadMain();

		/** Submits one chunk for an in-flight operation. Returns true on successful submit. */
		bool SubmitChunk(AsyncReadOperation* operation);

		/** Builds the result, completes the AsyncOp, notifies the owning stream, then returns the operation to the pool. */
		void FinalizeOperation(AsyncReadOperation* operation, size_t bytesRead, bool succeeded);

		/** Returns a freshly-constructed pooled operation slot, recycling a free one if available or growing the pool. Safe from any thread. */
		AsyncReadOperation* AllocateOperation();

		/** Destructs @p operation and returns its slot to the pool for reuse. Safe from any thread. */
		void ReleaseOperation(AsyncReadOperation* operation);

		struct io_uring mRing;
		int mEventFd = -1;
		Thread mReaper;
		Mutex mSubmitMutex; /**< Guards mPending only; not held across ring ops. */
		// Double-buffered MPSC drain: submitters push_back to mPending under mSubmitMutex; on each wake the reaper
		// O(1) swaps the two vectors, drains mReadyOperations unlocked, then clear()s it (preserving capacity). Both
		// vectors reach steady-state capacity after the first level-load burst and never reallocate afterwards.
		// FIFO ordering is preserved because submitters push_back in arrival order and the reaper iterates
		// front-to-back.
		Vector<AsyncReadOperation*> mPending;
		Vector<AsyncReadOperation*> mReadyOperations; /**< Reaper-only after the swap. */

		// Operation pool. Operations are pointer-stable (the pool never moves elements) and accessed from both submitter
		// threads (allocate) and the reaper (release), so a mutex guards it (TPool is not thread-safe on its own).
		Mutex mOperationMutex;
		TPool<AsyncReadOperation, 64> mOperationPool;

		std::atomic<bool> mRunning{false};
		std::atomic<bool> mStopping{false};
		u32 mWaitFailureCount = 0; /**< Reaper-local; log-throttle counter for io_uring_wait_cqe failures. */
	};

	/**
	 * Linux-specific file data stream. Inherits the synchronous POSIX surface from UnixFileDataStream and adds a native
	 * asynchronous ReadAsync() backed by the shared LinuxFileIOManager when FileAccessFlag::Async is set.
	 *
	 * When opened without Async, no async machinery is touched and ReadAsync() falls back to the synchronous default
	 * inherited from DataStream. When opened with Async, Open() checks that LinuxFileIOManager is running; if not (e.g.
	 * pre-5.1 kernel, sandboxed environment), the stream stays usable and ReadAsync() degrades to the synchronous
	 * fallback.
	 *
	 * The stream itself owns no ring/reaper - those live in the process-shared manager. The stream's per-instance state
	 * tracks only outstanding-read counting so Close() can drain in-flight reads before the fd is released.
	 *
	 * @note Close() is not safe to call concurrently from multiple threads (matches the Win32 stream contract). The
	 *       DataStream API is non-thread-safe by contract; the existing mAsyncMutex + CV cover the user-thread vs
	 *       reaper-thread boundary, which is the only one that actually exists.
	 */
	class LinuxFileDataStream final : public UnixFileDataStream
	{
	public:
		LinuxFileDataStream(const Path& filePath, FileAccessFlags access = FileAccessFlag::Read);
		~LinuxFileDataStream() override;

		/** Opens the file stream and (if Async was requested) checks the shared LinuxFileIOManager is running. */
		bool Open() override;
		TAsyncOp<TShared<MemoryDataStream>> ReadAsync(u64 offset, size_t byteCount, TOptional<DataRange> userSuppliedMemory = TOptional<DataRange>()) override;
		TShared<DataStream> Clone(bool copyData = true) const override;
		bool Close() override;

		/**
		 * Called by the LinuxFileIOManager from the reaper thread once an async read it accepted has finalized. Locks
		 * mAsyncMutex, decrements mOutstandingReads, and notifies the CV so a pending Close() drain can proceed.
		 */
		void OnAsyncReadComplete();

	private:
		Mutex mAsyncMutex;
		ConditionVariable mAllReadsComplete;
		u32 mOutstandingReads = 0;
		bool mAsyncReady = false;
		bool mClosed = false;
	};

	/** @} */
} // namespace b3d
