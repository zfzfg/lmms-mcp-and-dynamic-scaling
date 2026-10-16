#include "AiJobManager.h"
#include "AiFilePolicy.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace lmms::ai
{

QString jobStateToString(JobState state)
{
	switch (state)
	{
		case JobState::Queued: return "queued";
		case JobState::PreparingSnapshot: return "preparing_snapshot";
		case JobState::Running: return "running";
		case JobState::Validating: return "validating";
		case JobState::Succeeded: return "succeeded";
		case JobState::Failed: return "failed";
		case JobState::Cancelled: return "cancelled";
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

DomainError AiJobManager::startRenderJob(const QString& outputFileName,
                                         const QString& format,
                                         int sampleRate,
                                         int bitDepth,
                                         QString& outJobId)
{
	DomainError err;
	Q_UNUSED(bitDepth);

	if (!AiFilePolicy::instance()->isSafeFileName(outputFileName))
	{
		err.code = ErrorCode::ValidationError;
		err.message = QString("Unsafe or invalid output file name: %1").arg(outputFileName);
		return err;
	}

	QString finalPath;
	err = AiFilePolicy::instance()->validateAndResolve(AiFilePolicy::RootType::Exports, outputFileName, finalPath, false);
	if (err.hasError())
	{
		return err;
	}

	QString jobId = QString("job_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	outJobId = jobId;

	JobInfo info;
	info.jobId = jobId;
	info.type = "render";
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
	info.state = JobState::Running;

	QProcess* proc = new QProcess(this);
	m_processToJobId.insert(proc, jobId);

	connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
	        this, &AiJobManager::onProcessFinished);
	connect(proc, &QProcess::errorOccurred,
	        this, &AiJobManager::onProcessError);
	connect(proc, &QProcess::readyReadStandardError,
	        this, &AiJobManager::onProcessReadyRead);

	QStringList args;
	args << "-r" << info.snapshot.snapshotFilePath
	     << "-o" << info.tempOutputFile
	     << "-s" << QString::number(sampleRate > 0 ? sampleRate : 44100);

	proc->start(QCoreApplication::applicationFilePath(), args);

	{
		QMutexLocker locker(&m_mutex);
		m_jobs.insert(jobId, info);
	}

	return err;
}

bool AiJobManager::getJob(const QString& jobId, JobInfo& outInfo) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_jobs.contains(jobId))
	{
		return false;
	}
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
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
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

	if (info.state == JobState::Cancelled)
	{
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
	QString valError;
	if (!validateWavFile(info.tempOutputFile, valError))
	{
		info.state = JobState::Failed;
		info.error = QString("Validation of output audio failed: %1").arg(valError);
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	// Move temp file to final destination
	QFile::remove(info.finalOutputFile);
	if (!QFile::rename(info.tempOutputFile, info.finalOutputFile))
	{
		info.state = JobState::Failed;
		info.error = QString("Failed to move output to destination: %1").arg(info.finalOutputFile);
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		return;
	}

	info.state = JobState::Succeeded;
	info.progress = 100.0;
	info.artifactId = QString("art_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	info.artifactPath = info.finalOutputFile;

	AiProjectSnapshot::cleanupSnapshot(info.snapshot);
}

void AiJobManager::onProcessError(QProcess::ProcessError error)
{
	Q_UNUSED(error);
}

void AiJobManager::onProcessReadyRead()
{
	// Could parse progress percentages from stderr if available
}

bool AiJobManager::validateWavFile(const QString& filePath, QString& outError) const
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		outError = "Could not open output file for reading.";
		return false;
	}

	if (file.size() < 44)
	{
		outError = QString("File too small to be a valid WAV (%1 bytes)").arg(file.size());
		return false;
	}

	QByteArray header = file.read(44);
	if (header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE")
	{
		outError = "Header does not contain RIFF/WAVE signature.";
		return false;
	}

	return true;
}

} // namespace lmms::ai
