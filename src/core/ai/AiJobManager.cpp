#include "AiJobManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include "AiFilePolicy.h"
#include "AiProjectRevision.h"
#include "AiWavFile.h"
#include "Engine.h"

namespace lmms::ai {

QString jobStateToString(JobState state)
{
	switch (state)
	{
	case JobState::Queued:
		return "queued";
	case JobState::PreparingSnapshot:
		return "preparing_snapshot";
	case JobState::Running:
		return "running";
	case JobState::Validating:
		return "validating";
	case JobState::Succeeded:
		return "succeeded";
	case JobState::Failed:
		return "failed";
	case JobState::Cancelled:
		return "cancelled";
	}
	return "unknown";
}

QJsonObject JobInfo::toJson() const
{
	QJsonObject o;
	o["jobId"] = jobId;
	o["type"] = type;
	o["state"] = jobStateToString(state);
	o["progress"] = progress;
	o["projectRevision"] = static_cast<qint64>(snapshot.projectRevision);
	o["sha256"] = sha256;
	o["sizeBytes"] = sizeBytes;
	o["durationSeconds"] = durationSeconds;
	o["sampleRate"] = sampleRate;
	o["bitDepth"] = bitDepth;
	if (!error.isEmpty()) { o["error"] = error; }
	if (!artifactId.isEmpty()) { o["artifactId"] = artifactId; }
	if (!artifactPath.isEmpty()) { o["artifactPath"] = artifactPath; }
	return o;
}

AiJobManager* AiJobManager::instance()
{
	static AiJobManager s_instance;
	return &s_instance;
}

AiJobManager::AiJobManager()
	: QObject(nullptr)
{
}

DomainError AiJobManager::startRenderJob(
	const QString& outputFileName, const QString& format, int sampleRate, int bitDepth, QString& outJobId)
{
	DomainError err;
	if (format != "wav" || (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) || sampleRate < 8000
		|| sampleRate > 192000)
	{
		return {ErrorCode::ValidationError, "Expected WAV, 16/24/32 bits and sample rate 8000..192000."};
	}
	if (m_processToJobId.size() >= 2 || m_jobs.size() >= 128) { return {ErrorCode::Busy, "Render job limit reached."}; }

	if (!AiFilePolicy::instance()->isSafeFileName(outputFileName))
	{
		err.code = ErrorCode::ValidationError;
		err.message = QString("Unsafe or invalid output file name: %1").arg(outputFileName);
		return err;
	}

	QString finalPath;
	err = AiFilePolicy::instance()->validateAndResolve(
		AiFilePolicy::RootType::Exports, outputFileName, finalPath, false);
	if (err.hasError()) { return err; }

	QString jobId = QString("job_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	outJobId = jobId;

	JobInfo info;
	info.jobId = jobId;
	info.type = "render";
	info.bitDepth = bitDepth;
	info.sampleRate = sampleRate;
	info.state = JobState::PreparingSnapshot;
	info.createdAtEpochMs = QDateTime::currentMSecsSinceEpoch();
	info.finalOutputFile = finalPath;

	// Create snapshot
	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	DomainError snapErr = AiProjectSnapshot::createSnapshot(Engine::getSong(), currentRev, QString(), info.snapshot);
	if (snapErr.hasError())
	{
		info.state = JobState::Failed;
		info.error = snapErr.message;
		QMutexLocker locker(&m_mutex);
		m_jobs.insert(jobId, info);
		return snapErr;
	}

	// Prepare temp output file
	info.tempOutputFile = QDir(info.snapshot.tempDirectory).filePath("render_out.wav");
	info.state = JobState::PreparingSnapshot;

	QProcess* proc = new QProcess(this);
	m_processToJobId.insert(proc, jobId);

	connect(
		proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AiJobManager::onProcessFinished);
	connect(proc, &QProcess::errorOccurred, this, &AiJobManager::onProcessError);
	connect(proc, &QProcess::readyReadStandardOutput, this, &AiJobManager::onProcessReadyRead);
	connect(proc, &QProcess::readyReadStandardError, this, &AiJobManager::onProcessReadyRead);

	QStringList args;
	args << "--snapshot-manifest" << info.snapshot.manifestFilePath << "--no-mcp" << "--bitdepth"
		 << QString::number(bitDepth) << "--render-report" << QDir(info.snapshot.tempDirectory).filePath("outcome.json")
		 << "--config" << QDir(info.snapshot.tempDirectory).filePath("worker.xml") << "-r"
		 << info.snapshot.snapshotFilePath << "-o" << info.tempOutputFile << "-s"
		 << QString::number(sampleRate > 0 ? sampleRate : 44100);

	QTimer::singleShot(10 * 60 * 1000, proc, [this, proc, jobId] {
		if (m_processToJobId.contains(proc))
		{
			cancelJob(jobId);
			m_jobs[jobId].state = JobState::Failed;
			m_jobs[jobId].error = "Render exceeded the 10-minute worker budget.";
		}
	});

	{
		QMutexLocker locker(&m_mutex);
		m_jobs.insert(jobId, info);
	}

	proc->start(QCoreApplication::applicationFilePath(), args);
	return err;
}

bool AiJobManager::getJob(const QString& jobId, JobInfo& outInfo) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_jobs.contains(jobId)) { return false; }
	outInfo = m_jobs.value(jobId);
	return true;
}

DomainError AiJobManager::cancelJob(const QString& jobId)
{
	DomainError err;
	QMutexLocker locker(&m_mutex);
	if (!m_jobs.contains(jobId))
	{
		err.code = ErrorCode::ValidationError;
		err.message = QString("Job not found: %1").arg(jobId);
		return err;
	}

	JobInfo& info = m_jobs[jobId];
	if (info.state == JobState::Running || info.state == JobState::PreparingSnapshot)
	{
		// Find matching process
		for (auto it = m_processToJobId.begin(); it != m_processToJobId.end(); ++it)
		{
			if (it.value() == jobId)
			{
				QProcess* p = it.key();
				p->kill();
				break;
			}
		}
		info.state = JobState::Cancelled;
		info.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
	}

	return err;
}

void AiJobManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	QProcess* proc = qobject_cast<QProcess*>(sender());
	if (!proc) { return; }

	QString jobId = m_processToJobId.value(proc, QString());
	m_processToJobId.remove(proc);
	proc->deleteLater();

	if (jobId.isEmpty()) { return; }

	QMutexLocker locker(&m_mutex);
	if (!m_jobs.contains(jobId)) { return; }

	JobInfo& info = m_jobs[jobId];
	info.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();

	if (info.state == JobState::Cancelled || info.state == JobState::Failed)
	{
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	if (exitStatus != QProcess::NormalExit || exitCode != 0)
	{
		info.state = JobState::Failed;
		info.error = QString("Render process exited with code %1").arg(exitCode);
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	info.state = JobState::Validating;

	WavInfo wave;
	const auto waveError = inspectWavFile(info.tempOutputFile, wave, info.sampleRate, info.bitDepth);
	if (waveError.hasError())
	{
		info.state = JobState::Failed;
		info.error = QString("Validation of output audio failed: %1").arg(waveError.message);
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	QFile report(QDir(info.snapshot.tempDirectory).filePath("outcome.json"));
	if (!report.open(QIODevice::ReadOnly) || !QJsonDocument::fromJson(report.readAll()).object()["success"].toBool())
	{
		info.state = JobState::Failed;
		info.error = "Missing or failed worker outcome.";
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}
	QFile audio(info.tempOutputFile);
	if (!audio.open(QIODevice::ReadOnly))
	{
		info.state = JobState::Failed;
		info.error = "Cannot read rendered output.";
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	if (!digest.addData(&audio))
	{
		info.state = JobState::Failed;
		info.error = "Cannot hash rendered output.";
		audio.close();
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}
	audio.close();
	info.sha256 = QString::fromLatin1(digest.result().toHex());
	// Publish through an exclusively created and verified destination handle.
	QString published;
	const auto publishError = AiFilePolicy::instance()->publishNewFile(
		info.tempOutputFile, QFileInfo(info.finalOutputFile).fileName(), published);
	if (publishError.hasError())
	{
		info.state = JobState::Failed;
		info.error = QString("Failed to move output to destination: %1").arg(info.finalOutputFile);
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	info.state = JobState::Succeeded;
	info.progress = 100.0;
	info.durationSeconds = wave.durationSeconds;
	info.sizeBytes = QFileInfo(info.finalOutputFile).size();
	info.artifactId = QString("art_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	info.artifactPath = published;

	AiProjectSnapshot::cleanupSnapshot(info.snapshot);
}

void AiJobManager::onProcessError(QProcess::ProcessError error)
{
	if (error != QProcess::FailedToStart) { return; }
	auto* process = qobject_cast<QProcess*>(sender());
	const QString id = m_processToJobId.take(process);
	if (m_jobs.contains(id))
	{
		auto& job = m_jobs[id];
		job.state = JobState::Failed;
		job.error = process->errorString();
		job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
		AiProjectSnapshot::cleanupSnapshot(job.snapshot);
	}
	process->deleteLater();
}

void AiJobManager::onProcessReadyRead()
{
	if (auto* process = qobject_cast<QProcess*>(sender()))
	{
		const auto text = QString::fromUtf8(process->readAllStandardError().right(8192));
		process->readAllStandardOutput();
		const auto id = m_processToJobId.value(process);
		if (text.contains("LMMS_SNAPSHOT_READY") && m_jobs.contains(id)
			&& m_jobs[id].state == JobState::PreparingSnapshot)
		{
			m_jobs[id].state = JobState::Running;
		}
		auto matches = QRegularExpression("(\\d{1,3})%").globalMatch(text);
		while (matches.hasNext())
		{
			const int progress = matches.next().captured(1).toInt();
			if (m_jobs.contains(id)) { m_jobs[id].progress = qBound(0, progress, 99); }
		}
	}
}

void AiJobManager::shutdown()
{
	const auto processes = m_processToJobId.keys();
	for (auto* process : processes)
	{
		process->disconnect(this);
		process->kill();
		process->waitForFinished(3000);
		auto& job = m_jobs[m_processToJobId.take(process)];
		job.state = JobState::Cancelled;
		AiProjectSnapshot::cleanupSnapshot(job.snapshot);
		delete process;
	}
}

QJsonObject AiJobManager::readArtifact(const QString& artifactId, qint64 offset, int limit) const
{
	QMutexLocker lock(&m_mutex);
	for (const auto& job : m_jobs)
	{
		if (job.artifactId != artifactId || job.state != JobState::Succeeded) { continue; }
		QFile file(job.artifactPath);
		if (!AiFilePolicy::instance()->isPathAllowed(job.artifactPath, AiFilePolicy::RootType::Exports)
			|| !file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024 || offset < 0 || offset > file.size()
			|| limit < 1 || limit > 256 * 1024)
		{
			return {{"ok", false},
				{"error", DomainError{ErrorCode::ValidationError, "Invalid artifact range or size."}.toJson()}};
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		hash.addData(&file);
		if (QString::fromLatin1(hash.result().toHex()) != job.sha256)
		{
			return {{"ok", false},
				{"error", DomainError{ErrorCode::ExportValidationFailed, "Artifact was modified."}.toJson()}};
		}
		file.seek(offset);
		const auto bytes = file.read(limit);
		return {{"ok", true}, {"artifactId", artifactId}, {"offset", offset}, {"totalBytes", file.size()},
			{"data", QString::fromLatin1(bytes.toBase64())}, {"sha256", job.sha256},
			{"projectRevision", static_cast<qint64>(job.snapshot.projectRevision)}};
	}
	return {{"ok", false}, {"error", DomainError{ErrorCode::AssetNotFound, "Unknown artifact."}.toJson()}};
}

} // namespace lmms::ai
