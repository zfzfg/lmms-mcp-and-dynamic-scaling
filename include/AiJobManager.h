#ifndef LMMS_AI_JOB_MANAGER_H
#define LMMS_AI_JOB_MANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>
#include <memory>

#include "AiDomainTypes.h"
#include "AiProjectSnapshot.h"
#include "lmms_export.h"

namespace lmms::ai {

enum class JobState
{
	Queued,
	PreparingSnapshot,
	Running,
	Validating,
	Succeeded,
	Failed,
	Cancelled
};

LMMS_EXPORT QString jobStateToString(JobState state);

struct LMMS_EXPORT JobInfo
{
	QString jobId;
	QString type{"render"};
	JobState state{JobState::Queued};
	double progress{0.0};
	QString error;
	QString artifactId;
	QString artifactPath;
	QString sha256;
	qint64 sizeBytes{0};
	double durationSeconds{0};
	int sampleRate{44100};
	int bitDepth{16};
	SnapshotMetadata snapshot;
	QString tempOutputFile;
	QString finalOutputFile;
	qint64 createdAtEpochMs{0};
	qint64 finishedAtEpochMs{0};
	// Sealed when the succeeded artifact's size, mtime, and sha256 were captured.
	// Later reads compare size and mtime instead of hashing the file again.
	qint64 artifactMtimeMs{0};
	bool artifactSealed{false};
	// True only for the single export file this manager created and recorded.
	bool artifactOwned{false};

	QJsonObject toJson() const;
};

class LMMS_EXPORT AiJobManager : public QObject
{
	Q_OBJECT
public:
	static AiJobManager* instance();

	DomainError startRenderJob(
		const QString& outputFileName, const QString& format, int sampleRate, int bitDepth, QString& outJobId);

	bool getJob(const QString& jobId, JobInfo& outInfo) const;
	DomainError cancelJob(const QString& jobId);
	void shutdown();
	// Drops up to maxToRelease terminal jobs (failed, then cancelled, then the
	// oldest succeeded artifact that is not being read). Owned artifact files
	// are removed one-by-one; export directories are never deleted. Returns
	// how many jobs were released.
	int releaseFinishedJobs(int maxToRelease);
	// Safe off the GUI thread. Does not hash the whole file on every chunk.
	QJsonObject readArtifact(const QString& artifactId, qint64 offset, int limit);

	struct ArtifactFileRequest
	{
		QString path;
		QString sha256;
		qint64 offset = 0;
		int limit = 0;
		bool needHash = false;
		qint64 expectSize = 0;
		qint64 expectMtimeMs = 0;
	};

	struct ArtifactFileResult
	{
		bool ok = false;
		QString code;
		QString message;
		QByteArray bytes;
		qint64 size = 0;
		qint64 mtimeMs = 0;
	};

	// File IO only. No job mutex. Safe on a worker thread.
	static ArtifactFileResult readArtifactFile(const ArtifactFileRequest& request);

private slots:
	void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onProcessError(QProcess::ProcessError error);
	void onProcessReadyRead();

private:
	AiJobManager();
	~AiJobManager() override = default;

	bool isActiveState(JobState state) const;
	bool hasStartCapacityLocked() const;
	QString limitMessageLocked() const;
	int takeTerminalJobsLocked(int maxToRelease, QVector<JobInfo>& removed);
	void releaseJobFiles(const JobInfo& job) const;

	mutable QMutex m_mutex;
	QHash<QString, JobInfo> m_jobs;
	QHash<QProcess*, QString> m_processToJobId;
	QHash<QString, int> m_artifactReaders;
};

} // namespace lmms::ai

#endif // LMMS_AI_JOB_MANAGER_H
