/*
 * RemotePlugin.h - base class providing RPC like mechanisms
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

#ifndef LMMS_REMOTE_PLUGIN_H
#define LMMS_REMOTE_PLUGIN_H

#include <atomic>

#include <QThread>
#include <QProcess>
#include <QRecursiveMutex>

#include "RemotePluginBase.h"
#include "SharedMemory.h"
#include "LmmsTypes.h"

namespace lmms
{

class MidiEvent;
class RemotePlugin;
class SampleFrame;

class ProcessWatcher : public QThread
{
	Q_OBJECT
public:
	ProcessWatcher( RemotePlugin * );
	~ProcessWatcher() override = default;

	void stop()
	{
		m_quit = true;
		quit();
	}

	void reset()
	{
		m_quit = false;
	}

private:
	void run() override;

	RemotePlugin * m_plugin;
	volatile bool m_quit;

} ;


class LMMS_EXPORT RemotePlugin : public QObject, public RemotePluginBase
{
	Q_OBJECT
public:
	RemotePlugin();
	~RemotePlugin() override;

	inline bool isRunning()
	{
#ifdef DEBUG_REMOTE_PLUGIN
		return true;
#else
		return m_process.state() != QProcess::NotRunning;
#endif // DEBUG_REMOTE_PLUGIN
	}

	enum class State
	{
		Starting,
		Ready,
		Processing,
		Failed,
		Stopping,
		Stopped
	};

	State state() const
	{
		return m_state.load(std::memory_order_acquire);
	}

	// Audio deadlines missed while the plugin process was still alive.
	// A successful reinit clears this.
	int failureCount() const
	{
		return m_failureCount.load(std::memory_order_acquire);
	}

	bool init( const QString &pluginExecutable, bool waitForInitDoneMsg, QStringList extraArgs = {} );

	inline void waitForHostInfoGotten()
	{
		const bool ok = waitForMessage(IdHostInfoGotten, false,
						kRemotePluginInitTimeoutMs).id == IdHostInfoGotten;
		if (!ok)
		{
			failInitWait();
			return;
		}
		if (state() != State::Failed)
		{
			m_failed.store(false, std::memory_order_release);
		}
	}

	inline void waitForInitDone( bool _busyWaiting = true )
	{
		const bool ok = waitForMessage(IdInitDone, _busyWaiting,
						kRemotePluginInitTimeoutMs).id == IdInitDone;
		if (!ok)
		{
			failInitWait();
			return;
		}
		if (state() == State::Starting)
		{
			transitionTo(State::Ready);
		}
		if (state() == State::Ready)
		{
			m_failed.store(false, std::memory_order_release);
			m_failureCount.store(0, std::memory_order_release);
		}
	}

	bool processMessage( const message & _m ) override;

	bool process( const SampleFrame* _in_buf, SampleFrame* _out_buf );

	void processMidiEvent( const MidiEvent&, const f_cnt_t _offset );

	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		waitForMessage( IdInformationUpdated, true, kRemotePluginMessageTimeoutMs );
		unlock();
	}


	virtual void toggleUI()
	{
		lock();
		sendMessage( IdToggleUI );
		unlock();
	}

	int isUIVisible()
	{
		lock();
		sendMessage( IdIsUIVisible );
		unlock();
		message m = waitForMessage( IdIsUIVisible, false, kRemotePluginMessageTimeoutMs );
		return m.id != IdIsUIVisible ? -1 : m.getInt() ? 1 : 0;
	}

	inline bool failed() const
	{
		return m_failed.load(std::memory_order_acquire)
			|| m_state.load(std::memory_order_acquire) == State::Failed;
	}

	inline void lock()
	{
		m_commMutex.lock();
	}

	inline void unlock()
	{
		m_commMutex.unlock();
	}

public slots:
	virtual void showUI();
	virtual void hideUI();

protected:
	inline void setSplittedChannels( bool _on )
	{
		m_splitChannels = _on;
	}


	// Subclasses may record a failed load before the process starts.
	std::atomic<bool> m_failed{true};

private:
	bool transitionTo(State next);
	static bool isLegalTransition(State from, State to);
	void failInitWait();
	void failAudioWait(int deadlineMs);

	std::atomic<State> m_state{State::Stopped};
	std::atomic<int> m_failureCount{0};

	void resizeSharedProcessingMemory();


	QProcess m_process;
	ProcessWatcher m_watcher;

	QString m_exec;
	QStringList m_args;

	QRecursiveMutex m_commMutex;
	bool m_splitChannels;

	SharedMemory<float[]> m_audioBuffer;
	std::size_t m_audioBufferSize;

	int m_inputCount;
	int m_outputCount;

#ifndef SYNC_WITH_SHM_FIFO
	int m_server;
	QString m_socketFile;
#endif // not SYNC_WITH_SHM_FIFO

	friend class ProcessWatcher;


private slots:
	void processFinished( int exitCode, QProcess::ExitStatus exitStatus );
	void processErrored(QProcess::ProcessError err );
} ;

inline std::string QSTR_TO_STDSTR(QString const& qstr)
{
	return qstr.toStdString();
}

} // namespace lmms

#endif // LMMS_REMOTE_PLUGIN_H
