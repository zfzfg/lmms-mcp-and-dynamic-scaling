#ifndef LMMS_AI_JOB_MANAGER_H
#define LMMS_AI_JOB_MANAGER_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMutex>
#include <QHash>
#include <memory>
#include "lmms_export.h"
#include "AiDomainTypes.h"
#include "AiProjectSnapshot.h"

namespace lmms::ai
{

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

	DomainError startRenderJob(const QString& outputFileName,
	                           const QString& format,
	                           int sampleRate,
	                           int bitDepth,
	                           QString& outJobId);

	bool getJob(const QString& jobId, JobInfo& outInfo) const;
	DomainError cancelJob(const QString& jobId);

private slots:
	void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onProcessError(QProcess::ProcessError error);
	void onProcessReadyRead();

private:
	AiJobManager();
	~AiJobManager() override = default;

	bool validateWavFile(const QString& filePath, QString& outError) const;

	mutable QMutex m_mutex;
	QHash<QString, JobInfo> m_jobs;
	QHash<QProcess*, QString> m_processToJobId;
};

} // namespace lmms::ai

#endif // LMMS_AI_JOB_MANAGER_H
