/*
 * RemotePlugin.cpp - base class providing RPC like mechanisms
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "RemotePlugin.h"

#include <algorithm>
#include <cassert>

//#define DEBUG_REMOTE_PLUGIN
#ifdef DEBUG_REMOTE_PLUGIN
#include <QDebug>
#endif

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "Song.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUuid>

#ifndef SYNC_WITH_SHM_FIFO
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifdef LMMS_BUILD_WIN32

namespace {

HANDLE getRemotePluginJob()
{
	static const auto job = []
	{
		const auto job = CreateJobObject(nullptr, nullptr);

		auto limitInfo = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{};
		limitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limitInfo, sizeof(limitInfo));

		return job;
	}();

	return job;
}

} // namespace

#endif // LMMS_BUILD_WIN32

namespace lmms
{

// simple helper thread monitoring our RemotePlugin - if process terminates
// unexpectedly invalidate plugin so LMMS doesn't lock up
ProcessWatcher::ProcessWatcher( RemotePlugin * _p ) :
	QThread(),
	m_plugin( _p ),
	m_quit( false )
{
}


void ProcessWatcher::run()
{
	auto& process = m_plugin->m_process;
	process.start(m_plugin->m_exec, m_plugin->m_args);

#ifdef LMMS_BUILD_WIN32
	// Add the process to our job so it is killed if we crash
	if (process.waitForStarted(-1))
	{
		if (const auto processHandle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, false, process.processId()))
		{
			// Ensure the process is still running, otherwise the handle we
			// obtained may be for a different process that happened to reuse
			// the same process id.
			// QProcess::state() alone is insufficient as it only returns a
			// cached state variable that is updated asynchronously. To query
			// the process itself, we can use QProcess::waitForFinished() with a
			// zero timeout, but that too is insufficient as it fails if the
			// process has already finished. Therefore, we check both.
			if (!process.waitForFinished(0) && process.state() == QProcess::Running)
			{
				AssignProcessToJobObject(getRemotePluginJob(), processHandle);
			}
			CloseHandle(processHandle);
		}
	}
#endif // LMMS_BUILD_WIN32

	exec();
	process.moveToThread(m_plugin->thread());
	while (!m_quit && m_plugin->messagesLeft())
	{
		msleep(200);
	}
	if (!m_quit)
	{
		fprintf(stderr, "remote plugin died! invalidating now.\n");
		m_plugin->invalidate();
	}
}





RemotePlugin::RemotePlugin() :
	QObject(),
#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginBase( new shmFifo(), new shmFifo() ),
#else
	RemotePluginBase(),
#endif
	m_failed( true ),
	m_watcher( this ),
	m_splitChannels( false ),
	m_audioBufferSize( 0 ),
	m_inputCount( DEFAULT_CHANNELS ),
	m_outputCount( DEFAULT_CHANNELS )
{
#ifndef SYNC_WITH_SHM_FIFO
	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	m_socketFile = QDir::tempPath() + QDir::separator() +
						QUuid::createUuid().toString();
	auto path = m_socketFile.toUtf8();
	size_t length = path.length();
	if ( length >= sizeof sa.sun_path )
	{
		length = sizeof sa.sun_path - 1;
		qWarning( "Socket path too long." );
	}
	memcpy(sa.sun_path, path.constData(), length );
	sa.sun_path[length] = '\0';

	m_server = socket( PF_LOCAL, SOCK_STREAM, 0 );
	if ( m_server == -1 )
	{
		qWarning( "Unable to start the server." );
	}
	remove(path.constData());
	int ret = bind( m_server, (struct sockaddr *) &sa, sizeof sa );
	if ( ret == -1 || listen( m_server, 1 ) == -1 )
	{
		qWarning( "Unable to start the server." );
	}
#endif

	connect( &m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
		this, SLOT(processFinished(int,QProcess::ExitStatus)),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL(errorOccurred(QProcess::ProcessError)),
			 this, SLOT(processErrored(QProcess::ProcessError)),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
		&m_watcher, SLOT(quit()), Qt::DirectConnection );
}




RemotePlugin::~RemotePlugin()
{
	transitionTo(State::Stopping);
	m_watcher.stop();
	m_watcher.wait();

	if( isRunning() )
	{
		if( !isInvalid() )
		{
			lock();
			sendMessage( IdQuit );
			unlock();
		}
		// Existing QProcess handle. Escalate only after the shutdown budget.
		if( !m_process.waitForFinished( kRemotePluginShutdownTimeoutMs )
			&& m_process.state() != QProcess::NotRunning )
		{
			m_process.terminate();
			m_process.kill();
		}
	}
	if( !isInvalid() )
	{
		invalidate();
	}
	transitionTo(State::Stopped);

#ifndef SYNC_WITH_SHM_FIFO
	if ( close( m_server ) == -1)
	{
		qWarning( "Error freeing resources." );
	}
	remove( m_socketFile.toUtf8().constData() );
#endif
}




bool RemotePlugin::init(const QString &pluginExecutable,
							bool waitForInitDoneMsg , QStringList extraArgs)
{
	lock();
	if( m_failed.load() )
	{
#ifdef SYNC_WITH_SHM_FIFO
		reset( new shmFifo(), new shmFifo() );
#endif
		m_failed.store(false, std::memory_order_release);
	}
	if( !transitionTo(State::Starting) && state() != State::Starting )
	{
		m_failed.store(true, std::memory_order_release);
		unlock();
		return failed();
	}
	QString exec = QFileInfo(QDir("plugins:"), pluginExecutable).absoluteFilePath();

	// We may have received a directory via a environment variable
	if (const char* env_path = std::getenv("LMMS_PLUGIN_DIR"))
			exec = QFileInfo(QDir(env_path), pluginExecutable).absoluteFilePath();

#ifdef LMMS_BUILD_APPLE
	// search current directory first
	QString curDir = QCoreApplication::applicationDirPath() + "/" + pluginExecutable;
	if( QFile( curDir ).exists() )
	{
		exec = curDir;
	}
#endif
#ifdef LMMS_BUILD_WIN32
	if( ! exec.endsWith( ".exe", Qt::CaseInsensitive ) )
	{
		exec += ".exe";
	}
#endif

	if( ! QFile( exec ).exists() )
	{
		qWarning( "Remote plugin '%s' not found",
						exec.toUtf8().constData() );
		m_failed.store(true, std::memory_order_release);
		invalidate();
		transitionTo(State::Failed);
		unlock();
		return failed();
	}

	// ensure the watcher is ready in case we're running again
	// (e.g. 32-bit VST plugins on Windows)
	m_watcher.wait();
	m_watcher.reset();

	QStringList args;
#ifdef SYNC_WITH_SHM_FIFO
	// swap in and out for bidirectional communication
	args << QString::fromStdString(out()->shmKey());
	args << QString::fromStdString(in()->shmKey());
#else
	args << m_socketFile;
#endif
	args << extraArgs;
#ifndef DEBUG_REMOTE_PLUGIN
	m_process.setProcessChannelMode( QProcess::ForwardedChannels );
	m_process.setWorkingDirectory( QCoreApplication::applicationDirPath() );
	m_exec = exec;
	m_args = args;
	// we start the process on the watcher thread to work around QTBUG-8819
	m_process.moveToThread( &m_watcher );
	m_watcher.start( QThread::LowestPriority );
#else
	qDebug() << exec << args;
#endif

#ifndef SYNC_WITH_SHM_FIFO
	struct pollfd pollin;
	pollin.fd = m_server;
	pollin.events = POLLIN;

	// Already longer than the init-message budget; do not shorten it.
	switch ( poll( &pollin, 1, 30000 ) )
	{
		case -1:
			qWarning( "Unexpected poll error." );
			m_failed.store(true, std::memory_order_release);
			break;

		case 0:
			qWarning( "Remote plugin did not connect." );
			m_failed.store(true, std::memory_order_release);
			break;

		default:
			m_socket = accept( m_server, nullptr, nullptr );
			if ( m_socket == -1 )
			{
				qWarning( "Unexpected socket error." );
				m_failed.store(true, std::memory_order_release);
			}
	}
	if( m_failed.load() )
	{
		invalidate();
		transitionTo(State::Failed);
		unlock();
		return failed();
	}
#endif

	sendMessage(message(IdSyncKey).addString(Engine::getSong()->syncKey()));
	resizeSharedProcessingMemory();

	if( waitForInitDoneMsg )
	{
		waitForInitDone();
	}
	if( m_failed.load() )
	{
		transitionTo(State::Failed);
	}
	unlock();

	return failed();
}




namespace
{

int audioProcessingDeadlineMs(f_cnt_t frames, sample_rate_t rate)
{
	if (frames == 0 || rate == 0)
	{
		return kRemotePluginAudioTimeoutFallbackMs;
	}
	const auto twicePeriodMs = (static_cast<unsigned long long>(frames) * 2000ull)
		/ static_cast<unsigned long long>(rate);
	// Sub-millisecond periods still need one observable tick.
	if (twicePeriodMs == 0)
	{
		return 1;
	}
	if (twicePeriodMs > static_cast<unsigned long long>(kRemotePluginAudioTimeoutMaxMs))
	{
		return kRemotePluginAudioTimeoutMaxMs;
	}
	return static_cast<int>(twicePeriodMs);
}

} // namespace

bool RemotePlugin::process( const SampleFrame* _in_buf, SampleFrame* _out_buf )
{
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	const auto silence = [&]()
	{
		if( _out_buf != nullptr )
		{
			zeroSampleFrames(_out_buf, frames);
		}
		return false;
	};

	const State pluginState = state();
	// Starting stays silent so a short audio deadline cannot fail a plugin
	// that has not finished its init handshake. Failed does not wait again.
	if( m_failed.load(std::memory_order_acquire)
		|| pluginState == State::Failed
		|| pluginState == State::Starting
		|| pluginState == State::Stopping
		|| pluginState == State::Stopped )
	{
		return silence();
	}

	if( !isRunning() )
	{
		if( !isInvalid() )
		{
			invalidate();
		}
		m_failed.store(true, std::memory_order_release);
		transitionTo(State::Failed);
		return silence();
	}

	if (!m_audioBuffer)
	{
		// m_audioBuffer being zero means we didn't initialize everything so
		// far so process one message each time (and hope we get
		// information like SHM-key etc.) until we process messages
		// in a later stage of this procedure
		if( m_audioBufferSize == 0 )
		{
			lock();
			fetchAndProcessAllMessages();
			unlock();
		}
		if( _out_buf != nullptr )
		{
			zeroSampleFrames(_out_buf, frames);
		}
		return false;
	}

	lock();
	if( m_failed.load(std::memory_order_acquire) || state() != State::Ready
		|| !m_audioBuffer )
	{
		unlock();
		return silence();
	}

	memset( m_audioBuffer.get(), 0, m_audioBufferSize );

	ch_cnt_t inputs = std::min<ch_cnt_t>(m_inputCount, DEFAULT_CHANNELS);

	if( _in_buf != nullptr && inputs > 0 )
	{
		if( m_splitChannels )
		{
			for( ch_cnt_t ch = 0; ch < inputs; ++ch )
			{
				for( f_cnt_t frame = 0; frame < frames; ++frame )
				{
					m_audioBuffer[ch * frames + frame] =
							_in_buf[frame][ch];
				}
			}
		}
		else if( inputs == DEFAULT_CHANNELS )
		{
			auto target = m_audioBuffer.get();
			copyFromSampleFrames(target, _in_buf, frames);
		}
		else
		{
			auto o = (SampleFrame*)m_audioBuffer.get();
			for( ch_cnt_t ch = 0; ch < inputs; ++ch )
			{
				for( f_cnt_t frame = 0; frame < frames; ++frame )
				{
					o[frame][ch] = _in_buf[frame][ch];
				}
			}
		}
	}

	sendMessage( IdStartProcessing );

	if( _out_buf == nullptr || m_outputCount == 0 )
	{
		unlock();
		return silence();
	}

	if( !transitionTo(State::Processing) )
	{
		unlock();
		return silence();
	}

	// busy_waiting stays false: never pump processEvents() on the audio thread.
	const int budgetMs = audioProcessingDeadlineMs(frames,
		Engine::audioEngine()->outputSampleRate());
	const message done = waitForMessage(IdProcessingDone, false, budgetMs);
	if( done.id != IdProcessingDone )
	{
		if( state() != State::Stopping && state() != State::Stopped )
		{
			failAudioWait(budgetMs);
		}
		unlock();
		return silence();
	}
	transitionTo(State::Ready);

	const ch_cnt_t outputs = std::min<ch_cnt_t>(m_outputCount,
							DEFAULT_CHANNELS);
	if( m_splitChannels )
	{
		for( ch_cnt_t ch = 0; ch < outputs; ++ch )
		{
			for( f_cnt_t frame = 0; frame < frames; ++frame )
			{
				_out_buf[frame][ch] = m_audioBuffer[( m_inputCount+ch )*
								frames + frame];
			}
		}
	}
	else if( outputs == DEFAULT_CHANNELS )
	{
		auto source = m_audioBuffer.get() + m_inputCount * frames;
		copyToSampleFrames(_out_buf, source, frames);
	}
	else
	{
		auto o = (SampleFrame*)(m_audioBuffer.get() + m_inputCount * frames);
		// clear buffer, if plugin didn't fill up both channels
		zeroSampleFrames(_out_buf, frames);

		for (ch_cnt_t ch = 0; ch <
				std::min<int>(DEFAULT_CHANNELS, outputs); ++ch)
		{
			for( f_cnt_t frame = 0; frame < frames; ++frame )
			{
				_out_buf[frame][ch] = o[frame][ch];
			}
		}
	}

	unlock();
	return true;
}




void RemotePlugin::processMidiEvent( const MidiEvent & _e,
							const f_cnt_t _offset )
{
	message m( IdMidiEvent );
	m.addInt( _e.type() );
	m.addInt( _e.channel() );
	m.addInt( _e.param( 0 ) );
	m.addInt( _e.param( 1 ) );
	m.addInt( _offset );
	lock();
	sendMessage( m );
	unlock();
}

void RemotePlugin::showUI()
{
	lock();
	sendMessage( IdShowUI );
	unlock();
}

void RemotePlugin::hideUI()
{
	lock();
	sendMessage( IdHideUI );
	unlock();
}




void RemotePlugin::resizeSharedProcessingMemory()
{
	const size_t s = (m_inputCount + m_outputCount) * Engine::audioEngine()->framesPerPeriod();
	try
	{
		m_audioBuffer.create(s);
	}
	catch (const std::runtime_error& error)
	{
		qCritical() << "Failed to allocate shared audio buffer:" << error.what();
		m_audioBuffer.detach();
		return;
	}
	m_audioBufferSize = s * sizeof(float);
	sendMessage(message(IdChangeSharedMemoryKey).addString(m_audioBuffer.key()));
}




bool RemotePlugin::isLegalTransition(State from, State to)
{
	switch (from)
	{
		case State::Stopped:
			return to == State::Starting || to == State::Stopping;
		case State::Starting:
			return to == State::Ready || to == State::Failed || to == State::Stopping;
		case State::Ready:
			return to == State::Processing || to == State::Failed
				|| to == State::Stopping || to == State::Starting;
		case State::Processing:
			return to == State::Ready || to == State::Failed || to == State::Stopping;
		case State::Failed:
			return to == State::Starting || to == State::Stopping;
		case State::Stopping:
			return to == State::Stopped;
		default:
			return false;
	}
}

bool RemotePlugin::transitionTo(State next)
{
	State current = m_state.load(std::memory_order_acquire);
	while (true)
	{
		if (current == next)
		{
			return false;
		}
		if (!isLegalTransition(current, next))
		{
#if !defined(NDEBUG)
			// Shutdown and failure can win the race; those are not logic bugs.
			const bool terminal = current == State::Failed
				|| current == State::Stopping
				|| current == State::Stopped;
			if (!terminal)
			{
				assert(false && "illegal RemotePlugin state transition");
			}
#endif
			return false;
		}
		if (m_state.compare_exchange_weak(current, next,
				std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return true;
		}
	}
}

void RemotePlugin::failInitWait()
{
	const bool exited = !isRunning();
	m_failed.store(true, std::memory_order_release);
	if (!isInvalid())
	{
		invalidate();
	}
	transitionTo(State::Failed);
	if (exited)
	{
		qWarning() << "Remote plugin init failed because the process exited";
	}
	else
	{
		qWarning() << "Remote plugin init exceeded" << kRemotePluginInitTimeoutMs << "ms";
	}
}

void RemotePlugin::failAudioWait(int deadlineMs)
{
	const bool exited = !isRunning() || isInvalid();
	m_failed.store(true, std::memory_order_release);
	if (!exited)
	{
		const int count = m_failureCount.fetch_add(1, std::memory_order_acq_rel) + 1;
		fprintf(stderr,
			"Remote plugin missed the audio deadline of %d ms while its process was still running; isolating it (failure %d)\n",
			deadlineMs, count);
		invalidate();
	}
	else if (!isInvalid())
	{
		invalidate();
		fprintf(stderr, "Remote plugin process exited during audio processing\n");
	}
	transitionTo(State::Failed);
}

void RemotePlugin::processFinished( int exitCode,
					QProcess::ExitStatus exitStatus )
{
	if ( exitStatus == QProcess::CrashExit )
	{
		qCritical() << "Remote plugin crashed";
	}
	else if ( exitCode )
	{
		qCritical() << "Remote plugin exit code: " << exitCode;
	}
#ifndef SYNC_WITH_SHM_FIFO
	invalidate();
#endif
	if( state() != State::Stopping && state() != State::Stopped )
	{
		m_failed.store(true, std::memory_order_release);
		transitionTo(State::Failed);
	}
}

void RemotePlugin::processErrored( QProcess::ProcessError err )
{
	qCritical() << "Process error: " << err;
	if( state() == State::Stopping || state() == State::Stopped )
	{
		return;
	}
	m_failed.store(true, std::memory_order_release);
	if( !isInvalid() )
	{
		invalidate();
	}
	transitionTo(State::Failed);
}




bool RemotePlugin::processMessage( const message & _m )
{
	lock();
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdUndefined:
			unlock();
			return false;

		case IdInitDone:
			reply = true;
			break;

		case IdSampleRateInformation:
			reply = true;
			reply_message.addInt( Engine::audioEngine()->outputSampleRate() );
			break;

		case IdBufferSizeInformation:
			reply = true;
			reply_message.addInt( Engine::audioEngine()->framesPerPeriod() );
			break;

		case IdChangeInputCount:
			m_inputCount = _m.getInt( 0 );
			resizeSharedProcessingMemory();
			break;

		case IdChangeOutputCount:
			m_outputCount = _m.getInt( 0 );
			resizeSharedProcessingMemory();
			break;

		case IdChangeInputOutputCount:
			m_inputCount = _m.getInt( 0 );
			m_outputCount = _m.getInt( 1 );
			resizeSharedProcessingMemory();
			break;

		case IdDebugMessage:
			fprintf( stderr, "RemotePlugin::DebugMessage: %s",
						_m.getString( 0 ).c_str() );
			break;

		case IdProcessingDone:
		case IdQuit:
		default:
			break;
	}
	if( reply )
	{
		sendMessage( reply_message );
	}
	unlock();

	return true;
}


} // namespace lmms
