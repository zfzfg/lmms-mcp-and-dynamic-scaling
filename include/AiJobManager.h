#ifndef LMMS_AI_JOB_MANAGER_H
#define LMMS_AI_JOB_MANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QString>
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
	QJsonObject readArtifact(const QString& artifactId, qint64 offset, int limit) const;

private slots:
	void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onProcessError(QProcess::ProcessError error);
	void onProcessReadyRead();

private:
	AiJobManager();
	~AiJobManager() override = default;

	mutable QMutex m_mutex;
	QHash<QString, JobInfo> m_jobs;
	QHash<QProcess*, QString> m_processToJobId;
};

} // namespace lmms::ai

#endif // LMMS_AI_JOB_MANAGER_H
