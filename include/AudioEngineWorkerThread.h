/*
 * AudioEngineWorkerThread.h - declaration of class AudioEngineWorkerThread
 *
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_AUDIO_ENGINE_WORKER_THREAD_H
#define LMMS_AUDIO_ENGINE_WORKER_THREAD_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>

#include <QList>
#include <QThread>

#include "ThreadableJob.h"

namespace lmms
{

class AudioEngine;

// Synchronization contract:
// - One mutex and condition_variable cover worker wakeups and batch completion.
// - Workers sleep while !stop && generation is unchanged. quit() sets the stop
//   flag and notifies; it does not join threads or recycle the queue.
//   AudioEngine joins workers after quit() and does not free their jobs unless
//   the join succeeds.
// - A batch stays live while a worker is inside run()/process() or the queue
//   is not drained. tryResetJobQueue() fails for a live batch. Callers must
//   not delete those jobs or start another batch over them.
// - startAndWaitForJobs() refuses when a wait is already open or a worker is
//   still inside run(). Otherwise it bumps the generation, optionally runs
//   the inline share, then waits only until the armed absolute deadline.
//   The per-call budget is not added again for each mixer round.
// - Inline process() and a plugin call already inside the caller are not
//   preempted by that deadline. A timeout ends the wait, not the job.
// - The queue is initialized once. A worker constructor must not reset it.
// - addJob() claims an index, then release-stores the job pointer only after
//   queue(). Consumers acquire that pointer by exchanging the slot to nullptr.
//   A published non-null pointer is processed exactly once.
// - Indexes past JOB_QUEUE_SIZE are not queued. The claim is still counted
//   finished so wait cannot hang, and the job is left unqueued.
struct RenderWaitAccount
{
	std::int64_t inlineNanoseconds = 0;
	std::int64_t waitNanoseconds = 0;
	bool ranInline = false;
	bool skippedInlineBecauseDeadline = false;
	bool waitBudgetExhausted = false;
	bool deadlineAlreadyExpired = false;
};

class AudioEngineWorkerThread : public QThread
{
	Q_OBJECT
public:
	// internal representation of the job queue - all functions are thread-safe
	class JobQueue
	{
	public:
		enum class OperationMode
		{
			Static,	// no jobs added while processing queue
			Dynamic	// jobs can be added while processing queue
		} ;

		static constexpr size_t JOB_QUEUE_SIZE = 8192;

		JobQueue();

		void reset( OperationMode opMode );

		// true: job was published and will be processed by a worker.
		// false: job was not queued. The caller must process it inline when
		// it still requires processing. Overflow does not leave the job Queued.
		bool addJob( ThreadableJob * job );

		bool readyToReset() const noexcept { return drained(); }

		void run();
		void wait();
		bool waitFor( std::chrono::milliseconds timeout );

		std::uint64_t overflowCount() const
		{
			return m_overflowCount.load( std::memory_order_relaxed );
		}

	private:
		void advancePublished() noexcept;
		void finishOne() noexcept;
		bool drained() const noexcept;
		std::size_t publishedLimit() const noexcept;
		bool batchSettled() const;

		std::atomic<ThreadableJob *> m_items[JOB_QUEUE_SIZE];
		// 1 once the pointer at that index has been release-stored.
		// Consumers only scan the consecutive published prefix, so they
		// never observe a hole and never need to exchange a not-yet-stored slot.
		std::atomic<std::uint8_t> m_ready[JOB_QUEUE_SIZE];
		std::atomic<std::size_t> m_writeIndex;
		std::atomic<std::size_t> m_published;
		std::atomic<std::size_t> m_itemsDone;
		std::atomic<std::uint64_t> m_overflowCount;
		// Bumped by reset so a scan still inside run() after a timed-out
		// wait stops claiming slots and stops touching the new counters.
		std::atomic<std::uint64_t> m_epoch;
		std::atomic<OperationMode> m_opMode;
	} ;


	AudioEngineWorkerThread( AudioEngine * audioEngine );
	~AudioEngineWorkerThread() override;

	//! One-time queue setup. Safe to call more than once. Does not bump the
	//! epoch again after the first call, so a new worker cannot invalidate a live batch.
	static void initializeQueueOnce();

	//! False when another AudioEngine already owns the process-wide queue.
	static bool tryAcquireEngineOwnership();
	static void releaseEngineOwnership();

	//! Empty time point clears the deadline. startAndWaitForJobs then uses @p budget
	//! from the moment it is called, after subtracting inline time.
	static void armRenderDeadline(std::chrono::steady_clock::time_point deadline);
	static bool deadlineReached();

	virtual void quit();

	// False while workers are inside run() or the queue still holds unfinished jobs.
	// A false result means the caller must keep the current jobs alive.
	static bool tryResetJobQueue( JobQueue::OperationMode opMode =
													JobQueue::OperationMode::Static );

	static bool addJob( ThreadableJob * job )
	{
		return globalJobQueue.addJob( job );
	}

	// Resets only when the previous batch is idle, then publishes @p items.
	// False means the previous batch is still live and nothing was queued.
	template<typename T>
	static bool fillJobQueue( const T & items,
							JobQueue::OperationMode opMode = JobQueue::OperationMode::Static )
	{
		if( !tryResetJobQueue( opMode ) )
		{
			return false;
		}
		bool skippedRequiredJob = false;
		for( const auto & job : items )
		{
			// addJob() reserves an index before queue(), and only then
			// release-stores the pointer. false means it was not published:
			// either requiresProcessing() was false, or the queue was full.
			// A full-queue reservation is already counted finished. Run that
			// job inline once on this thread. Do not queue() a job that was
			// accepted, or it would be processed twice.
			if( !addJob( job ) && job->requiresProcessing() )
			{
				// Overflow is synchronous on this thread and counts as inline work.
				// Past the deadline it is not started; the caller must fail the period.
				if( deadlineReached() )
				{
					noteSkippedInlineJob();
					skippedRequiredJob = true;
				}
				else
				{
					job->queue();
					job->process();
				}
			}
		}
		return !skippedRequiredJob;
	}

	// A non-positive budget uses two audio periods, clamped to 2..50 ms.
	// @p runInline processes part of the queue on the caller. Tests pass false
	// so a worker is the one inside process() while the caller only waits.
	// False: the budget expired, or a batch is already in progress. The queue
	// was not reset. Do not call again until batchInFlight() is false.
	static bool startAndWaitForJobs( std::chrono::milliseconds budget = std::chrono::milliseconds::zero(),
									bool runInline = true );

	// True while a worker is inside run() or a timed-out batch is not drained.
	// Also true after jobs have been queued and before they finish.
	static bool batchInFlight();

	static int aliveWorkers();

	// True after a timed-out wait until a later wait succeeds.
	static bool lastWaitTimedOut();

	static std::uint64_t waitTimeouts();
	static std::uint64_t busyRefusals();
	static std::uint64_t deadlineMisses();
	static std::uint64_t incompleteJobs();
	static RenderWaitAccount lastWaitAccount();

	// Cumulative number of addJob() calls that did not fit in the queue.
	static std::uint64_t overflowCount();

	//! Two audio periods, clamped to 2..50 ms. This is a wait budget, not a
	//! guarantee that inline plugin code returns inside it.
	static std::chrono::milliseconds renderWaitBudget();

	//! Test isolation. Does nothing while a worker is inside run() or a wait is open.
	static bool resetProcessStateForTests();

	static void noteSkippedInlineJob();


private:
	void run() override;

	static JobQueue globalJobQueue;
	static std::mutex s_mutex;
	static std::condition_variable s_cv;
	static std::uint64_t s_generation;
	static std::size_t s_alive;
	static std::size_t s_expected;
	static std::size_t s_finished;
	static std::size_t s_workersInRun;
	static bool s_waitInProgress;
	static std::atomic<bool> s_lastWaitTimedOut;
	static std::atomic<std::uint64_t> s_waitTimeouts;
	static std::atomic<std::uint64_t> s_busyRefusals;
	static std::atomic<std::uint64_t> s_deadlineMisses;
	static std::atomic<std::uint64_t> s_incompleteJobs;
	static RenderWaitAccount s_lastWaitAccount;
	static std::chrono::steady_clock::time_point s_deadline;
	static std::atomic<int> s_engineOwners;
	static QList<AudioEngineWorkerThread *> workerThreads;

	std::atomic<bool> m_stop;
} ;

} // namespace lmms

#endif // LMMS_AUDIO_ENGINE_WORKER_THREAD_H
