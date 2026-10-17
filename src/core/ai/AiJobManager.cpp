#include "AiJobManager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <functional>

#include "AiFilePolicy.h"
#include "AiProjectRevision.h"
#include "AiWavFile.h"
#include "Engine.h"

namespace lmms::ai {

namespace {
constexpr int kMaxActiveJobs = 2;
constexpr int kMaxJobs = 128;
constexpr qint64 kMaxArtifactBytes = 512LL * 1024 * 1024;
constexpr int kMaxArtifactChunkBytes = 256 * 1024;

QJsonObject artifactError(ErrorCode code, const QString& message)
{
	return {{"ok", false}, {"error", DomainError{code, message}.toJson()}};
}

// Removes one recorded export file. Never deletes a directory or an unowned path.
void removeOwnedArtifactFile(const JobInfo& job)
{
	if (!job.artifactOwned || job.artifactPath.isEmpty()) { return; }
	const QFileInfo info(job.artifactPath);
	if (!info.exists() || !info.isFile() || info.isSymLink() || info.isDir()) { return; }
	const QString canonical = info.canonicalFilePath();
	if (canonical.isEmpty()) { return; }
	const QString recorded = QDir::cleanPath(job.artifactPath);
	const QString absolute = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
	const bool sameFile = canonical.compare(recorded, Qt::CaseInsensitive) == 0
		|| canonical.compare(absolute, Qt::CaseInsensitive) == 0;
#else
	const bool sameFile = canonical == recorded || canonical == absolute;
#endif
	if (!sameFile) { return; }
	if (!AiFilePolicy::instance()->isPathAllowed(canonical, AiFilePolicy::RootType::Exports)) { return; }
	QFile::remove(canonical);
}
} // namespace

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

bool AiJobManager::isActiveState(JobState state) const
{
	switch (state)
	{
	case JobState::Queued:
	case JobState::PreparingSnapshot:
	case JobState::Running:
	case JobState::Validating:
		return true;
	case JobState::Succeeded:
	case JobState::Failed:
	case JobState::Cancelled:
		return false;
	}
	// Unknown states stay resident so a new state cannot be dropped by accident.
	return true;
}

bool AiJobManager::hasStartCapacityLocked() const
{
	if (m_processToJobId.size() >= kMaxActiveJobs) { return false; }
	if (m_jobs.size() < kMaxJobs) { return true; }
	for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
	{
		if (isActiveState(it->state) || m_artifactReaders.value(it.key()) > 0) { continue; }
		return true;
	}
	return false;
}

QString AiJobManager::limitMessageLocked() const
{
	int active = 0;
	int retained = 0;
	for (const JobInfo& job : m_jobs)
	{
		if (isActiveState(job.state)) { ++active; }
		else { ++retained; }
	}
	return QString("%1 active %2 and %3 retained %4. Cancelling an active job or waiting frees a slot.")
		.arg(active)
		.arg(active == 1 ? QString("job") : QString("jobs"))
		.arg(retained)
		.arg(retained == 1 ? QString("job") : QString("jobs"));
}

int AiJobManager::takeTerminalJobsLocked(int maxToRelease, QVector<JobInfo>& removed)
{
	if (maxToRelease <= 0) { return 0; }
	struct Candidate
	{
		QString id;
		int tier{0};
		qint64 when{0};
	};
	QVector<Candidate> candidates;
	for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
	{
		if (isActiveState(it->state) || m_artifactReaders.value(it.key()) > 0) { continue; }
		int tier = -1;
		if (it->state == JobState::Failed) { tier = 0; }
		else if (it->state == JobState::Cancelled) { tier = 1; }
		else if (it->state == JobState::Succeeded) { tier = 2; }
		else { continue; }
		const qint64 when = it->finishedAtEpochMs > 0 ? it->finishedAtEpochMs : it->createdAtEpochMs;
		candidates.append(Candidate{it.key(), tier, when});
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
		if (a.tier != b.tier) { return a.tier < b.tier; }
		if (a.when != b.when) { return a.when < b.when; }
		return a.id < b.id;
	});
	int count = 0;
	for (const Candidate& candidate : candidates)
	{
		if (count >= maxToRelease || !m_jobs.contains(candidate.id)) { continue; }
		const JobInfo job = m_jobs.value(candidate.id);
		if (isActiveState(job.state) || m_artifactReaders.value(candidate.id) > 0) { continue; }
		removed.append(job);
		m_jobs.remove(candidate.id);
		m_artifactReaders.remove(candidate.id);
		++count;
	}
	return count;
}

void AiJobManager::releaseJobFiles(const JobInfo& job) const
{
	removeOwnedArtifactFile(job);
	// Snapshot cleanup only accepts this process's lmms_snapshot_* temp directory.
	AiProjectSnapshot::cleanupSnapshot(job.snapshot);
}

int AiJobManager::releaseFinishedJobs(int maxToRelease)
{
	QVector<JobInfo> removed;
	int count = 0;
	{
		QMutexLocker locker(&m_mutex);
		count = takeTerminalJobsLocked(maxToRelease, removed);
	}
	for (const JobInfo& job : removed) { releaseJobFiles(job); }
	return count;
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

	{
		QMutexLocker locker(&m_mutex);
		// Active renders stay capped at 2. Terminal jobs do not consume that cap,
		// but the retained history must not grow without an evictable slot.
		if (!hasStartCapacityLocked()) { return {ErrorCode::Busy, limitMessageLocked()}; }
	}

	const QString jobId = QString("job_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	outJobId = jobId;

	JobInfo info;
	info.jobId = jobId;
	info.type = "render";
	info.bitDepth = bitDepth;
	info.sampleRate = sampleRate;
	info.state = JobState::PreparingSnapshot;
	info.createdAtEpochMs = QDateTime::currentMSecsSinceEpoch();
	info.finalOutputFile = finalPath;

	const uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	const DomainError snapErr
		= AiProjectSnapshot::createSnapshot(Engine::getSong(), currentRev, QString(), info.snapshot);
	if (snapErr.hasError())
	{
		info.state = JobState::Failed;
		info.error = snapErr.message;
		info.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
		QVector<JobInfo> removed;
		{
			QMutexLocker locker(&m_mutex);
			if (m_jobs.size() >= kMaxJobs) { takeTerminalJobsLocked(m_jobs.size() - (kMaxJobs - 1), removed); }
			if (m_jobs.size() < kMaxJobs) { m_jobs.insert(jobId, info); }
			else { removed.append(info); }
		}
		for (const JobInfo& job : removed) { releaseJobFiles(job); }
		return snapErr;
	}

	info.tempOutputFile = QDir(info.snapshot.tempDirectory).filePath("render_out.wav");
	info.state = JobState::PreparingSnapshot;

	QVector<JobInfo> removed;
	QString blockedMessage;
	QProcess* proc = nullptr;
	{
		QMutexLocker locker(&m_mutex);
		if (m_processToJobId.size() < kMaxActiveJobs && m_jobs.size() >= kMaxJobs)
		{
			takeTerminalJobsLocked(m_jobs.size() - (kMaxJobs - 1), removed);
		}
		if (m_processToJobId.size() >= kMaxActiveJobs || m_jobs.size() >= kMaxJobs)
		{
			blockedMessage = limitMessageLocked();
		}
		else
		{
			proc = new QProcess(this);
			m_processToJobId.insert(proc, jobId);
			m_jobs.insert(jobId, info);
		}
	}
	for (const JobInfo& job : removed) { releaseJobFiles(job); }
	if (!proc)
	{
		AiProjectSnapshot::cleanupSnapshot(info.snapshot);
		if (blockedMessage.isEmpty()) { blockedMessage = "Render job limit reached."; }
		return {ErrorCode::Busy, blockedMessage};
	}

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
		{
			QMutexLocker locker(&m_mutex);
			if (!m_processToJobId.contains(proc)) { return; }
		}
		cancelJob(jobId);
		QMutexLocker locker(&m_mutex);
		if (!m_jobs.contains(jobId)) { return; }
		JobInfo& job = m_jobs[jobId];
		if (job.state == JobState::Succeeded) { return; }
		job.state = JobState::Failed;
		job.error = "Render exceeded the 10-minute worker budget.";
		job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
	});

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
	QProcess* process = nullptr;
	{
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
			for (auto it = m_processToJobId.begin(); it != m_processToJobId.end(); ++it)
			{
				if (it.value() == jobId)
				{
					process = it.key();
					break;
				}
			}
			info.state = JobState::Cancelled;
			info.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
		}
	}
	if (process) { process->kill(); }
	return err;
}

void AiJobManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	QProcess* proc = qobject_cast<QProcess*>(sender());
	if (!proc) { return; }

	QString jobId;
	JobInfo info;
	bool stopEarly = false;
	{
		QMutexLocker locker(&m_mutex);
		jobId = m_processToJobId.take(proc);
		if (jobId.isEmpty() || !m_jobs.contains(jobId)) { stopEarly = true; }
		else
		{
			info = m_jobs.value(jobId);
			if (info.state == JobState::Cancelled || info.state == JobState::Failed) { stopEarly = true; }
			else { m_jobs[jobId].state = JobState::Validating; }
		}
	}
	proc->deleteLater();
	if (jobId.isEmpty()) { return; }
	if (stopEarly)
	{
		releaseJobFiles(info);
		return;
	}

	const auto fail = [this, jobId](const QString& message, const QString& publishedPath) {
		SnapshotMetadata snapshot;
		{
			QMutexLocker locker(&m_mutex);
			if (!m_jobs.contains(jobId)) { snapshot = SnapshotMetadata(); }
			else
			{
				JobInfo& job = m_jobs[jobId];
				if (job.state != JobState::Cancelled && job.state != JobState::Failed)
				{
					job.state = JobState::Failed;
					job.error = message;
					job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
				}
				snapshot = job.snapshot;
			}
		}
		if (!publishedPath.isEmpty())
		{
			JobInfo orphan;
			orphan.artifactOwned = true;
			orphan.artifactPath = publishedPath;
			removeOwnedArtifactFile(orphan);
		}
		AiProjectSnapshot::cleanupSnapshot(snapshot);
	};

	if (exitStatus != QProcess::NormalExit || exitCode != 0)
	{
		fail(QString("Render process exited with code %1").arg(exitCode), QString());
		return;
	}

	WavInfo wave;
	const auto waveError = inspectWavFile(info.tempOutputFile, wave, info.sampleRate, info.bitDepth);
	if (waveError.hasError())
	{
		fail(QString("Validation of output audio failed: %1").arg(waveError.message), QString());
		return;
	}

	QFile report(QDir(info.snapshot.tempDirectory).filePath("outcome.json"));
	if (!report.open(QIODevice::ReadOnly) || !QJsonDocument::fromJson(report.readAll()).object()["success"].toBool())
	{
		fail("Missing or failed worker outcome.", QString());
		return;
	}
	report.close();

	QString published;
	const auto publishError = AiFilePolicy::instance()->publishNewFile(
		info.tempOutputFile, QFileInfo(info.finalOutputFile).fileName(), published);
	if (publishError.hasError())
	{
		fail(QString("Failed to move output to destination: %1").arg(info.finalOutputFile), QString());
		return;
	}

	QFile audio(published);
	if (!audio.open(QIODevice::ReadOnly))
	{
		fail("Cannot read rendered output.", published);
		return;
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	if (!digest.addData(&audio))
	{
		audio.close();
		fail("Cannot hash rendered output.", published);
		return;
	}
	const QString sha256 = QString::fromLatin1(digest.result().toHex());
	const qint64 sealedSize = audio.size();
	audio.close();
	QFileInfo sealedInfo(published);
	sealedInfo.refresh();
	const qint64 sealedMtime = sealedInfo.lastModified().toMSecsSinceEpoch();
	const bool owned = sealedInfo.isFile() && !sealedInfo.isSymLink()
		&& AiFilePolicy::instance()->isPathAllowed(published, AiFilePolicy::RootType::Exports);

	SnapshotMetadata snapshot;
	bool committed = false;
	{
		QMutexLocker locker(&m_mutex);
		if (m_jobs.contains(jobId))
		{
			JobInfo& job = m_jobs[jobId];
			snapshot = job.snapshot;
			if (job.state != JobState::Cancelled && job.state != JobState::Failed)
			{
				// Visible as Succeeded only after the integrity reference is stored.
				job.sha256 = sha256;
				job.sizeBytes = sealedSize;
				job.artifactMtimeMs = sealedMtime;
				job.artifactSealed = true;
				job.artifactOwned = owned;
				job.durationSeconds = wave.durationSeconds;
				job.progress = 100.0;
				job.artifactId = QString("art_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
				job.artifactPath = published;
				job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
				job.state = JobState::Succeeded;
				committed = true;
			}
		}
	}
	if (!committed && !published.isEmpty())
	{
		JobInfo orphan;
		orphan.artifactOwned = true;
		orphan.artifactPath = published;
		removeOwnedArtifactFile(orphan);
	}
	AiProjectSnapshot::cleanupSnapshot(snapshot);
}

void AiJobManager::onProcessError(QProcess::ProcessError error)
{
	if (error != QProcess::FailedToStart) { return; }
	auto* process = qobject_cast<QProcess*>(sender());
	if (!process) { return; }
	SnapshotMetadata snapshot;
	{
		QMutexLocker locker(&m_mutex);
		const QString id = m_processToJobId.take(process);
		if (m_jobs.contains(id))
		{
			JobInfo& job = m_jobs[id];
			job.state = JobState::Failed;
			job.error = process->errorString();
			job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
			snapshot = job.snapshot;
		}
	}
	process->deleteLater();
	AiProjectSnapshot::cleanupSnapshot(snapshot);
}

void AiJobManager::onProcessReadyRead()
{
	auto* process = qobject_cast<QProcess*>(sender());
	if (!process) { return; }
	const auto text = QString::fromUtf8(process->readAllStandardError().right(8192));
	process->readAllStandardOutput();
	QMutexLocker locker(&m_mutex);
	const auto id = m_processToJobId.value(process);
	if (id.isEmpty() || !m_jobs.contains(id)) { return; }
	if (text.contains("LMMS_SNAPSHOT_READY") && m_jobs[id].state == JobState::PreparingSnapshot)
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

void AiJobManager::shutdown()
{
	QList<QProcess*> processes;
	{
		QMutexLocker locker(&m_mutex);
		processes = m_processToJobId.keys();
	}
	for (QProcess* process : processes)
	{
		process->disconnect(this);
		process->kill();
		process->waitForFinished(3000);
		SnapshotMetadata snapshot;
		{
			QMutexLocker locker(&m_mutex);
			const QString jobId = m_processToJobId.take(process);
			if (m_jobs.contains(jobId))
			{
				JobInfo& job = m_jobs[jobId];
				job.state = JobState::Cancelled;
				job.finishedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
				snapshot = job.snapshot;
			}
		}
		AiProjectSnapshot::cleanupSnapshot(snapshot);
		// deleteLater pairs with the finished handler. A direct delete can race it.
		process->deleteLater();
	}
}

AiJobManager::ArtifactFileResult AiJobManager::readArtifactFile(const ArtifactFileRequest& request)
{
	ArtifactFileResult result;
	if (request.offset < 0 || request.limit < 1 || request.limit > kMaxArtifactChunkBytes)
	{
		result.code = QStringLiteral("invalid");
		result.message = QStringLiteral("Invalid artifact range or size.");
		return result;
	}
	const QFileInfo info(request.path);
	if (!info.exists() || !info.isFile() || info.isSymLink())
	{
		result.code = QStringLiteral("invalid");
		result.message = QStringLiteral("Invalid artifact range or size.");
		return result;
	}
	const qint64 size = info.size();
	const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
	if (request.offset > size || size > kMaxArtifactBytes)
	{
		result.code = QStringLiteral("invalid");
		result.message = QStringLiteral("Invalid artifact range or size.");
		return result;
	}
	if (request.needHash)
	{
		QFile hashed(request.path);
		if (!hashed.open(QIODevice::ReadOnly))
		{
			result.code = QStringLiteral("invalid");
			result.message = QStringLiteral("Invalid artifact range or size.");
			return result;
		}
		QCryptographicHash digest(QCryptographicHash::Sha256);
		if (!digest.addData(&hashed))
		{
			result.code = QStringLiteral("modified");
			result.message = QStringLiteral("Artifact was modified.");
			return result;
		}
		const QString actual = QString::fromLatin1(digest.result().toHex());
		const QFileInfo afterHash(request.path);
		if (actual != request.sha256 || hashed.size() != size || afterHash.size() != size
			|| afterHash.lastModified().toMSecsSinceEpoch() != mtime)
		{
			result.code = QStringLiteral("modified");
			result.message = QStringLiteral("Artifact was modified.");
			return result;
		}
	}
	else if (size != request.expectSize || mtime != request.expectMtimeMs)
	{
		result.code = QStringLiteral("modified");
		result.message = QStringLiteral("Artifact was modified.");
		return result;
	}

	QFile file(request.path);
	if (!file.open(QIODevice::ReadOnly) || file.size() != size || !file.seek(request.offset))
	{
		result.code = QStringLiteral("invalid");
		result.message = QStringLiteral("Invalid artifact range or size.");
		return result;
	}
	result.bytes = file.read(request.limit);
	if (file.error() != QFileDevice::NoError)
	{
		result.code = QStringLiteral("invalid");
		result.message = QStringLiteral("Invalid artifact range or size.");
		return result;
	}
	file.close();
	const QFileInfo afterRead(request.path);
	if (afterRead.size() != size || afterRead.lastModified().toMSecsSinceEpoch() != mtime || afterRead.isSymLink())
	{
		result.code = QStringLiteral("modified");
		result.message = QStringLiteral("Artifact was modified.");
		return result;
	}
	result.ok = true;
	result.size = size;
	result.mtimeMs = mtime;
	return result;
}

QJsonObject AiJobManager::readArtifact(const QString& artifactId, qint64 offset, int limit)
{
	QString path;
	QString sha256;
	QString jobId;
	qint64 projectRevision = 0;
	{
		QMutexLocker lock(&m_mutex);
		bool found = false;
		for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
		{
			if (it->artifactId != artifactId || it->state != JobState::Succeeded) { continue; }
			path = it->artifactPath;
			sha256 = it->sha256;
			jobId = it->jobId;
			projectRevision = static_cast<qint64>(it->snapshot.projectRevision);
			found = true;
			break;
		}
		if (!found) { return artifactError(ErrorCode::AssetNotFound, "Unknown artifact."); }
		if (offset < 0 || limit < 1 || limit > kMaxArtifactChunkBytes)
		{
			return artifactError(ErrorCode::ValidationError, "Invalid artifact range or size.");
		}
	}

	if (!AiFilePolicy::instance()->isPathAllowed(path, AiFilePolicy::RootType::Exports))
	{
		return artifactError(ErrorCode::ValidationError, "Invalid artifact range or size.");
	}
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile() || info.isSymLink())
	{
		return artifactError(ErrorCode::ValidationError, "Invalid artifact range or size.");
	}
	const qint64 size = info.size();
	const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
	if (offset > size) { return artifactError(ErrorCode::ValidationError, "Invalid artifact range or size."); }

	bool needHash = false;
	{
		QMutexLocker lock(&m_mutex);
		auto it = m_jobs.find(jobId);
		if (it == m_jobs.end() || it->artifactId != artifactId || it->state != JobState::Succeeded
			|| it->artifactPath != path)
		{
			return artifactError(ErrorCode::AssetNotFound, "Unknown artifact.");
		}
		// Size and mtime are the integrity check. A changed file is never accepted,
		// including when the replacement is also within the byte ceiling.
		if (it->artifactSealed && (it->sizeBytes != size || it->artifactMtimeMs != mtime || it->sha256 != sha256))
		{
			return artifactError(ErrorCode::ExportValidationFailed, "Artifact was modified.");
		}
		if (size > kMaxArtifactBytes || (it->artifactSealed && it->sizeBytes > kMaxArtifactBytes))
		{
			return artifactError(ErrorCode::ValidationError, "Artifact exceeds the 512 MiB limit.");
		}
		if (!it->artifactSealed) { needHash = true; }
		m_artifactReaders[jobId] = m_artifactReaders.value(jobId) + 1;
	}

	std::function<void()> endRead = [this, jobId]() {
		QMutexLocker lock(&m_mutex);
		const int left = m_artifactReaders.value(jobId) - 1;
		if (left <= 0) { m_artifactReaders.remove(jobId); }
		else { m_artifactReaders.insert(jobId, left); }
	};
	struct ReadGuard
	{
		std::function<void()> fn;
		explicit ReadGuard(std::function<void()> callback)
			: fn(std::move(callback))
		{
		}
		~ReadGuard()
		{
			if (fn) { fn(); }
		}
	} guard(std::move(endRead));

	const ArtifactFileResult fileResult = readArtifactFile(ArtifactFileRequest{
		path, sha256, offset, limit, needHash, size, mtime});
	if (!fileResult.ok)
	{
		const auto code = fileResult.code == QLatin1String("modified") ? ErrorCode::ExportValidationFailed
																		: ErrorCode::ValidationError;
		return artifactError(code, fileResult.message);
	}
	if (needHash)
	{
		QMutexLocker lock(&m_mutex);
		if (!m_jobs.contains(jobId) || m_jobs[jobId].artifactPath != path || m_jobs[jobId].state != JobState::Succeeded
			|| m_jobs[jobId].sha256 != sha256)
		{
			return artifactError(ErrorCode::ExportValidationFailed, "Artifact was modified.");
		}
		JobInfo& job = m_jobs[jobId];
		if (!job.artifactSealed)
		{
			job.sizeBytes = fileResult.size;
			job.artifactMtimeMs = fileResult.mtimeMs;
			job.artifactSealed = true;
		}
		else if (job.sizeBytes != fileResult.size || job.artifactMtimeMs != fileResult.mtimeMs)
		{
			return artifactError(ErrorCode::ExportValidationFailed, "Artifact was modified.");
		}
	}
	const QByteArray bytes = fileResult.bytes;
	const qint64 reportedSize = fileResult.size;

	const bool complete = offset + static_cast<qint64>(bytes.size()) >= reportedSize;
	return {{"ok", true}, {"artifactId", artifactId}, {"offset", offset}, {"totalBytes", reportedSize},
		{"data", QString::fromLatin1(bytes.toBase64())}, {"sha256", sha256}, {"projectRevision", projectRevision},
		{"complete", complete}};
}

} // namespace lmms::ai
