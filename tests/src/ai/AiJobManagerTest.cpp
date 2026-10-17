#include <QtTest>
#include "AiJobManager.h"

class AiJobManagerTest : public QObject
{
	Q_OBJECT
private slots:
	void testJobStateStrings()
	{
		using namespace lmms::ai;
		QCOMPARE(jobStateToString(JobState::Queued), QString("queued"));
		QCOMPARE(jobStateToString(JobState::Running), QString("running"));
		QCOMPARE(jobStateToString(JobState::Succeeded), QString("succeeded"));
		QCOMPARE(jobStateToString(JobState::Failed), QString("failed"));
		QCOMPARE(jobStateToString(JobState::Cancelled), QString("cancelled"));
	}

	void testJobInfoJson()
	{
		using namespace lmms::ai;
		JobInfo info;
		info.jobId = "job_1234";
		info.type = "render";
		info.state = JobState::Running;
		info.progress = 45.5;

		QJsonObject o = info.toJson();
		QCOMPARE(o["jobId"].toString(), QString("job_1234"));
		QCOMPARE(o["type"].toString(), QString("render"));
		QCOMPARE(o["state"].toString(), QString("running"));
		QCOMPARE(o["progress"].toDouble(), 45.5);
	}

	void testInvalidFileNameRejected()
	{
		using namespace lmms::ai;
		auto mgr = AiJobManager::instance();
		QString jobId;

		DomainError err = mgr->startRenderJob("../escaped.wav", "wav", 44100, 16, jobId);
		QVERIFY(err.hasError());
		QCOMPARE(err.code, ErrorCode::ValidationError);
	}

	void testNonExistentJob()
	{
		using namespace lmms::ai;
		auto mgr = AiJobManager::instance();
		JobInfo info;
		bool found = mgr->getJob("job_non_existent", info);
		QVERIFY(!found);
	}
};

QTEST_GUILESS_MAIN(AiJobManagerTest)
#include "AiJobManagerTest.moc"
