/*
 * AudioEngineWorkerThread.cpp - implementation of AudioEngineWorkerThread
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

#include "AudioEngineWorkerThread.h"

#include <QList>

#include "AudioEngine.h"
#include "Engine.h"
#include "Hardware.h"
#include "ThreadableJob.h"


namespace lmms
{

namespace
{
std::chrono::milliseconds renderWaitBudget()
{
	auto* engine = Engine::audioEngine();
	if (engine == nullptr || engine->outputSampleRate() <= 0 || engine->framesPerPeriod() <= 0)
	{
		return std::chrono::milliseconds(20);
	}
	const auto periodMs = (static_cast<long long>(engine->framesPerPeriod()) * 1000) / engine->outputSampleRate();
	const auto budget = periodMs * 2;
	if (budget < 2) { return std::chrono::milliseconds(2); }
	if (budget > 50) { return std::chrono::milliseconds(50); }
	return std::chrono::milliseconds(static_cast<int>(budget));
}
}


AudioEngineWorkerThread::JobQueue AudioEngineWorkerThread::globalJobQueue;
std::mutex AudioEngineWorkerThread::s_mutex;
std::condition_variable AudioEngineWorkerThread::s_cv;
std::uint64_t AudioEngineWorkerThread::s_generation = 0;
std::size_t AudioEngineWorkerThread::s_alive = 0;
std::size_t AudioEngineWorkerThread::s_expected = 0;
std::size_t AudioEngineWorkerThread::s_finished = 0;
std::size_t AudioEngineWorkerThread::s_workersInRun = 0;
bool AudioEngineWorkerThread::s_waitInProgress = false;
std::atomic<bool> AudioEngineWorkerThread::s_lastWaitTimedOut{ false };
std::atomic<std::uint64_t> AudioEngineWorkerThread::s_waitTimeouts{ 0 };
std::atomic<std::uint64_t> AudioEngineWorkerThread::s_busyRefusals{ 0 };
QList<AudioEngineWorkerThread *> AudioEngineWorkerThread::workerThreads;

AudioEngineWorkerThread::JobQueue::JobQueue() :
	m_writeIndex( 0 ),
	m_published( 0 ),
	m_itemsDone( 0 ),
	m_overflowCount( 0 ),
	m_epoch( 0 ),
	m_opMode( OperationMode::Static )
{
	for( auto & slot : m_items )
	{
		slot.store( nullptr, std::memory_order_relaxed );
	}
	for( auto & ready : m_ready )
	{
		ready.store( 0, std::memory_order_relaxed );
	}
}




void AudioEngineWorkerThread::JobQueue::reset( OperationMode opMode )
{
	// Invalidate in-flight scans before recycling slots. A successful
	// startAndWaitForJobs() has already waited for those scans to leave
	// run(); this only matters after a timed-out wait, which must not be
	// followed by a reset from inside the wait itself.
	m_epoch.fetch_add( 1, std::memory_order_acq_rel );

	const auto reserved = m_writeIndex.load( std::memory_order_relaxed );
	const auto limit = reserved < JOB_QUEUE_SIZE ? reserved : JOB_QUEUE_SIZE;
	for( std::size_t i = 0; i < limit; ++i )
	{
		m_items[i].store( nullptr, std::memory_order_relaxed );
		m_ready[i].store( 0, std::memory_order_relaxed );
	}

	m_published.store( 0, std::memory_order_relaxed );
	m_itemsDone.store( 0, std::memory_order_relaxed );
	m_writeIndex.store( 0, std::memory_order_relaxed );
	m_opMode.store( opMode, std::memory_order_release );
}




void AudioEngineWorkerThread::JobQueue::advancePublished() noexcept
{
	auto published = m_published.load( std::memory_order_relaxed );
	while( published < JOB_QUEUE_SIZE
		&& m_ready[published].load( std::memory_order_acquire ) != 0 )
	{
		if( m_published.compare_exchange_strong(
				published,
				published + 1,
				std::memory_order_release,
				std::memory_order_relaxed ) )
		{
			++published;
		}
	}
}




void AudioEngineWorkerThread::JobQueue::finishOne() noexcept
{
	const auto done = m_itemsDone.fetch_add( 1, std::memory_order_release ) + 1;
	if( done < m_writeIndex.load( std::memory_order_acquire ) )
	{
		return;
	}

	// Pair with the mutex in wait()/waitFor() so the completion wakeup
	// cannot land before the waiter has evaluated its predicate.
	const std::lock_guard<std::mutex> lock( s_mutex );
	s_cv.notify_all();
}




bool AudioEngineWorkerThread::JobQueue::drained() const noexcept
{
	// Sample the reservation count around the done count. addJob() bumps
	// writeIndex before the reserving process() can finish, so a stable
	// pair with done >= reserved means every claim has completed.
	for( ;; )
	{
		const auto reserved = m_writeIndex.load( std::memory_order_acquire );
		const auto done = m_itemsDone.load( std::memory_order_acquire );
		const auto reservedAgain = m_writeIndex.load( std::memory_order_acquire );
		if( reserved == reservedAgain )
		{
			return done >= reserved;
		}
	}
}




std::size_t AudioEngineWorkerThread::JobQueue::publishedLimit() const noexcept
{
	const auto published = m_published.load( std::memory_order_acquire );
	return published < JOB_QUEUE_SIZE ? published : JOB_QUEUE_SIZE;
}




bool AudioEngineWorkerThread::JobQueue::batchSettled() const
{
	// Caller holds s_mutex. A stop flag must not report success while jobs
	// are still unfinished; the owner joins threads and only then frees them.
	return drained() && s_workersInRun == 0 && s_finished >= s_expected;
}




bool AudioEngineWorkerThread::JobQueue::addJob( ThreadableJob * job )
{
	if( !job->requiresProcessing() )
	{
		return false;
	}

	const auto index = m_writeIndex.fetch_add( 1, std::memory_order_acq_rel );
	if( index >= JOB_QUEUE_SIZE )
	{
		// Count the failed claim as finished. writeIndex includes it, so
		// leaving it unfinished would wedge wait(). Do not call queue().
		m_overflowCount.fetch_add( 1, std::memory_order_relaxed );
		finishOne();
		return false;
	}

	// queue() happens-before the release store. The consumer that acquires
	// the pointer therefore observes ProcessingState::Queued.
	job->queue();
	m_items[index].store( job, std::memory_order_release );
	m_ready[index].store( 1, std::memory_order_release );
	advancePublished();
	return true;
}




void AudioEngineWorkerThread::JobQueue::run()
{
	const auto epoch = m_epoch.load( std::memory_order_acquire );
	const bool dynamic = m_opMode.load( std::memory_order_acquire ) == OperationMode::Dynamic;

	for( ;; )
	{
		if( m_epoch.load( std::memory_order_acquire ) != epoch )
		{
			return;
		}

		const auto limit = publishedLimit();
		for( std::size_t i = 0; i < limit; ++i )
		{
			if( m_epoch.load( std::memory_order_acquire ) != epoch )
			{
				return;
			}

			// acq_rel: acquire the producer's release store, and release
			// the nullptr so the slot is empty for a later batch.
			ThreadableJob * job = m_items[i].exchange( nullptr, std::memory_order_acq_rel );
			if( job == nullptr )
			{
				continue;
			}
			if( m_epoch.load( std::memory_order_acquire ) != epoch )
			{
				// Slot was recycled. Run the job we already claimed so it
				// is not left Queued, but do not account it on the new batch.
				job->process();
				return;
			}

			job->process();
			if( m_epoch.load( std::memory_order_acquire ) != epoch )
			{
				return;
			}
			finishOne();
		}

		if( !dynamic )
		{
			break;
		}

		// Jobs reserved from inside process() show up as a longer prefix.
		// Scan again or a pointer published after this pass would be skipped
		// until the next generation, and wait() would time out.
		if( publishedLimit() != limit )
		{
			continue;
		}
		break;
	}
}




void AudioEngineWorkerThread::JobQueue::wait()
{
	std::unique_lock<std::mutex> lock( s_mutex );
	s_cv.wait( lock, [this]() { return batchSettled(); } );
}




bool AudioEngineWorkerThread::JobQueue::waitFor( std::chrono::milliseconds timeout )
{
	std::unique_lock<std::mutex> lock( s_mutex );
	return s_cv.wait_for( lock, timeout, [this]() { return batchSettled(); } );
}




// implementation of worker threads

AudioEngineWorkerThread::AudioEngineWorkerThread( AudioEngine * audioEngine ) :
	QThread( audioEngine ),
	m_stop( false )
{
	// The last worker AudioEngine constructs is never started. Its share of
	// the queue is processed inline by startAndWaitForJobs() so the render
	// thread does not synchronize with another thread for that portion.
	workerThreads << this;

	globalJobQueue.reset( JobQueue::OperationMode::Static );
}




AudioEngineWorkerThread::~AudioEngineWorkerThread()
{
	workerThreads.removeAll( this );
}




void AudioEngineWorkerThread::quit()
{
	{
		const std::lock_guard<std::mutex> lock( s_mutex );
		m_stop.store( true, std::memory_order_release );
	}
	s_cv.notify_all();
}




bool AudioEngineWorkerThread::tryResetJobQueue( JobQueue::OperationMode opMode )
{
	const std::lock_guard<std::mutex> lock( s_mutex );
	if( s_waitInProgress || s_workersInRun != 0 || !globalJobQueue.readyToReset() )
	{
		s_busyRefusals.fetch_add( 1, std::memory_order_relaxed );
		return false;
	}
	globalJobQueue.reset( opMode );
	return true;
}




bool AudioEngineWorkerThread::startAndWaitForJobs( std::chrono::milliseconds budget, bool runInline )
{
	if( budget.count() <= 0 )
	{
		budget = renderWaitBudget();
	}

	{
		const std::lock_guard<std::mutex> lock( s_mutex );
		if( s_waitInProgress || s_workersInRun != 0 )
		{
			s_busyRefusals.fetch_add( 1, std::memory_order_relaxed );
			s_lastWaitTimedOut.store( true, std::memory_order_release );
			return false;
		}
		s_waitInProgress = true;
		++s_generation;
		s_finished = 0;
		s_expected = s_alive;
	}
	s_cv.notify_all();

	struct WaitGuard
	{
		~WaitGuard()
		{
			const std::lock_guard<std::mutex> lock( s_mutex );
			s_waitInProgress = false;
		}
	} guard;

	// Inline portion on the calling audio thread. Workers run the same queue
	// concurrently and claim each published pointer with exchange.
	if( runInline )
	{
		globalJobQueue.run();
	}

	if( !globalJobQueue.waitFor( budget ) )
	{
		// Leave the queue and the workers alone. A later tryResetJobQueue()
		// fails until process() has returned and the queue is drained.
		s_waitTimeouts.fetch_add( 1, std::memory_order_relaxed );
		s_lastWaitTimedOut.store( true, std::memory_order_release );
		return false;
	}
	s_lastWaitTimedOut.store( false, std::memory_order_release );
	return true;
}




bool AudioEngineWorkerThread::batchInFlight()
{
	const std::lock_guard<std::mutex> lock( s_mutex );
	return s_waitInProgress || s_workersInRun != 0 || !globalJobQueue.readyToReset();
}




int AudioEngineWorkerThread::aliveWorkers()
{
	const std::lock_guard<std::mutex> lock( s_mutex );
	return static_cast<int>( s_alive );
}




bool AudioEngineWorkerThread::lastWaitTimedOut()
{
	return s_lastWaitTimedOut.load( std::memory_order_acquire );
}




std::uint64_t AudioEngineWorkerThread::waitTimeouts()
{
	return s_waitTimeouts.load( std::memory_order_relaxed );
}




std::uint64_t AudioEngineWorkerThread::busyRefusals()
{
	return s_busyRefusals.load( std::memory_order_relaxed );
}




std::uint64_t AudioEngineWorkerThread::overflowCount()
{
	return globalJobQueue.overflowCount();
}




void AudioEngineWorkerThread::run()
{
	disableDenormals();

	std::unique_lock<std::mutex> lock( s_mutex );
	++s_alive;
	auto seenGeneration = s_generation;

	while( !m_stop.load( std::memory_order_acquire ) )
	{
		s_cv.wait( lock, [this, &seenGeneration]()
		{
			return m_stop.load( std::memory_order_acquire )
				|| s_generation != seenGeneration;
		} );
		if( m_stop.load( std::memory_order_acquire ) )
		{
			break;
		}

		seenGeneration = s_generation;
		const auto generation = seenGeneration;
		++s_workersInRun;
		lock.unlock();

		globalJobQueue.run();

		lock.lock();
		--s_workersInRun;
		// A newer startAndWaitForJobs() owns s_finished. Ack only the
		// generation we actually ran.
		if( s_generation == generation )
		{
			++s_finished;
		}
		s_cv.notify_all();
	}

	--s_alive;
	s_cv.notify_all();
}

} // namespace lmms
