/*
 * AudioEngineProfiler.cpp - class for profiling performance of AudioEngine
 *
 * Copyright (c) 2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioEngineProfiler.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

namespace lmms
{

struct AudioEngineProfiler::PeriodExport
{
	struct Record
	{
		uint32_t processUs = 0;
		uint32_t lockWaitUs = 0;
		uint32_t limitUs = 0;
	};

	static constexpr size_t kCapacity = 4096;

	std::array<Record, kCapacity> ring{};
	std::atomic<uint64_t> writeIndex{0};
	std::atomic<uint64_t> readIndex{0};
	std::atomic<bool> stop{false};
	std::atomic<bool> enabled{false};
	std::mutex pathMutex;
	std::string path;
	std::thread worker;

	void ensureStarted()
	{
		if (worker.joinable()) { return; }
		worker = std::thread([this] { drain(); });
	}

	~PeriodExport()
	{
		stop.store(true, std::memory_order_release);
		if (worker.joinable()) { worker.join(); }
	}

	void push(Record record)
	{
		if (!enabled.load(std::memory_order_relaxed)) { return; }
		const auto write = writeIndex.load(std::memory_order_relaxed);
		const auto read = readIndex.load(std::memory_order_acquire);
		if (write - read >= kCapacity) { return; }
		ring[write % kCapacity] = record;
		writeIndex.store(write + 1, std::memory_order_release);
	}

	void drain()
	{
		std::ofstream out;
		std::string openPath;
		while (!stop.load(std::memory_order_acquire) || readIndex.load(std::memory_order_relaxed) != writeIndex.load(std::memory_order_acquire))
		{
			std::string requested;
			{
				const std::lock_guard<std::mutex> lock(pathMutex);
				requested = path;
			}
			if (requested != openPath)
			{
				out.close();
				openPath = requested;
				if (!openPath.empty()) { out.open(openPath, std::ios::out | std::ios::trunc); }
			}

			const auto read = readIndex.load(std::memory_order_relaxed);
			const auto write = writeIndex.load(std::memory_order_acquire);
			if (read == write)
			{
				if (stop.load(std::memory_order_acquire)) { break; }
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}
			const Record record = ring[read % kCapacity];
			readIndex.store(read + 1, std::memory_order_release);
			if (out.is_open())
			{
				out << record.processUs << ' ' << record.lockWaitUs << ' ' << record.limitUs << '\n';
			}
		}
	}
};

AudioEngineProfiler::AudioEngineProfiler()
	: m_periodTimer()
	, m_cpuLoad(0)
	, m_export(std::make_unique<PeriodExport>())
{
}

AudioEngineProfiler::~AudioEngineProfiler() = default;

void AudioEngineProfiler::accountSample(unsigned int processUs, unsigned int lockWaitUs, unsigned int limitUs)
{
	m_lastProcessUs.store(processUs, std::memory_order_relaxed);
	m_lastLockWaitUs.store(lockWaitUs, std::memory_order_relaxed);

	if (limitUs == 0) { return; }
	const auto total = static_cast<uint64_t>(processUs) + lockWaitUs;
	if (total > limitUs) { m_deadlineMisses.fetch_add(1, std::memory_order_relaxed); }

	const auto percent = (100.0 * static_cast<double>(total)) / static_cast<double>(limitUs);
	const size_t bucket = percent < 25.0 ? 0 : percent < 50.0 ? 1 : percent < 70.0 ? 2 : percent < 100.0 ? 3 : 4;
	m_histogram[bucket].fetch_add(1, std::memory_order_relaxed);

	// Smoothed load stays processing-only so a lock wait is visible in the deadline counter
	// even while the displayed load remains low.
	const auto newCpuLoad = 100.f * processUs / limitUs;
	const auto previous = m_cpuLoad.load(std::memory_order_relaxed);
	m_cpuLoad.store(newCpuLoad * 0.1f + previous * 0.9f, std::memory_order_relaxed);
}

void AudioEngineProfiler::recordSample(unsigned int processUs, unsigned int lockWaitUs, unsigned int limitUs)
{
	accountSample(processUs, lockWaitUs, limitUs);
}

void AudioEngineProfiler::noteBackendXrun()
{
	m_backendXruns.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngineProfiler::noteWorkerStall()
{
	m_workerStalls.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngineProfiler::histogram(std::array<uint64_t, 5>& buckets) const
{
	for (size_t i = 0; i < buckets.size(); ++i)
	{
		buckets[i] = m_histogram[i].load(std::memory_order_relaxed);
	}
}

void AudioEngineProfiler::finishPeriod( sample_rate_t sampleRate, f_cnt_t framesPerPeriod )
{
	// Time taken to process all data and fill the audio buffer.
	const unsigned int periodElapsed = static_cast<unsigned int>(std::max(0, m_periodTimer.elapsed()));
	// Maximum time the processing can take before causing buffer underflow. Convert to us.
	const uint64_t timeLimit = sampleRate > 0 ? static_cast<uint64_t>(1000000) * framesPerPeriod / sampleRate : 0;
	const auto limitUs = static_cast<unsigned int>(std::min<uint64_t>(timeLimit, UINT32_MAX));
	accountSample(periodElapsed, m_lockWaitUs, limitUs);

	// Compute detailed load analysis. Can use stronger averaging to get more stable readout.
	for (std::size_t i = 0; i < DetailCount; i++)
	{
		const auto newLoad = timeLimit > 0 ? 100.f * m_detailTime[i] / timeLimit : 0.f;
		const auto oldLoad = m_detailLoad[i].load(std::memory_order_relaxed);
		m_detailLoad[i].store(newLoad * 0.05f + oldLoad * 0.95f, std::memory_order_relaxed);
	}

	if (m_export)
	{
		m_export->push(PeriodExport::Record{periodElapsed, m_lockWaitUs, limitUs});
	}
}

void AudioEngineProfiler::setOutputFile( const QString& outputFile )
{
	if (!m_export) { return; }
	{
		const std::lock_guard<std::mutex> lock(m_export->pathMutex);
		m_export->path = outputFile.toStdString();
	}
	m_export->enabled.store(!outputFile.isEmpty(), std::memory_order_release);
	if (!outputFile.isEmpty()) { m_export->ensureStarted(); }
}

} // namespace lmms
