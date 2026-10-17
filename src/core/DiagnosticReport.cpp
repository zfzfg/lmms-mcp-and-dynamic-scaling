/*
 * DiagnosticReport.cpp - local support report without project contents or secrets
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "DiagnosticReport.h"

#include <array>

#include <QFile>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextStream>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "Engine.h"
#include "lmmsconfig.h"
#include "lmmsversion.h"
#include "versioninfo.h"

namespace lmms
{

auto DiagnosticReport::text(DiagnosticExtras extras) -> QString
{
	QString report;
	QTextStream out(&report);
	out << "LMMS diagnostic report\n";
	out << "version: " << LMMS_VERSION << "\n";
	out << "compiler: " << LMMS_BUILDCONF_COMPILER_VERSION << "\n";
	out << "qt: " << QT_VERSION_STR << "\n";
	out << "os: " << QSysInfo::prettyProductName() << "\n";
	out << "kernel: " << QSysInfo::kernelVersion() << "\n";
	out << "cpu: " << QSysInfo::currentCpuArchitecture() << "\n";
	out << "Project paths, samples and connection tokens are omitted.\n";
	if (Engine::audioEngine() != nullptr)
	{
		auto& profiler = Engine::audioEngine()->profiler();
		std::array<uint64_t, 5> buckets{};
		profiler.histogram(buckets);
		out << "sampleRate: " << Engine::audioEngine()->outputSampleRate() << "\n";
		out << "framesPerPeriod: " << Engine::audioEngine()->framesPerPeriod() << "\n";
		out << "cpuLoadSmoothed: " << profiler.cpuLoad() << "\n";
		out << "lastProcessUs: " << profiler.lastProcessUs() << "\n";
		out << "lastLockWaitUs: " << profiler.lastLockWaitUs() << "\n";
		out << "deadlineMisses: " << profiler.deadlineMisses() << "\n";
		out << "backendXruns: " << profiler.backendXruns() << "\n";
		out << "workerStalls: " << profiler.workerStalls() << "\n";
		out << "histogramUnder25_50_70_100_over: " << buckets[0] << " " << buckets[1] << " " << buckets[2] << " "
			<< buckets[3] << " " << buckets[4] << "\n";
		out << "workerBatchInFlight: " << (AudioEngineWorkerThread::batchInFlight() ? 1 : 0) << "\n";
		out << "workerWaitTimeouts: " << AudioEngineWorkerThread::waitTimeouts() << "\n";
		out << "workerBusyRefusals: " << AudioEngineWorkerThread::busyRefusals() << "\n";
		out << "workerOverflows: " << AudioEngineWorkerThread::overflowCount() << "\n";
	}
	if (extras.guiLagMs >= 0) { out << "guiLagMs: " << extras.guiLagMs << "\n"; }
	if (extras.guiLagMaxMs >= 0) { out << "guiLagMaxMs: " << extras.guiLagMaxMs << "\n"; }
	return report;
}

auto DiagnosticReport::writeTo(const QString& path, QString* error, DiagnosticExtras extras) -> bool
{
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error) { *error = file.errorString(); }
		return false;
	}
	const auto bytes = text(extras).toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		if (error) { *error = file.errorString(); }
		return false;
	}
	return true;
}

} // namespace lmms
