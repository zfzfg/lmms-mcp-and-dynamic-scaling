#include <QtTest>
#include "AiProjectRevision.h"

class AiProjectRevisionTest : public QObject
{
	Q_OBJECT
private slots:
	void testMonotonicIncrease()
	{
		using namespace lmms::ai;
		auto rev = AiProjectRevision::instance();
		rev->resetProject("test_proj_1");

		uint64_t initial = rev->currentRevision();
		QCOMPARE(initial, static_cast<uint64_t>(1));

		rev->bumpRevision("edit 1");
		QCOMPARE(rev->currentRevision(), static_cast<uint64_t>(2));

		rev->bumpRevision("edit 2");
		QCOMPARE(rev->currentRevision(), static_cast<uint64_t>(3));
	}

	void testSignalEmission()
	{
		using namespace lmms::ai;
		auto rev = AiProjectRevision::instance();
		rev->resetProject("test_proj_sig");

		QSignalSpy spy(rev, &AiProjectRevision::revisionChanged);
		rev->bumpRevision("test_signal");

		QCOMPARE(spy.count(), 1);
		QList<QVariant> args = spy.takeFirst();
		QCOMPARE(args.at(0).toULongLong(), static_cast<uint64_t>(2));
		QCOMPARE(args.at(1).toString(), QString("test_signal"));
	}

	void testClipRevisionTracking()
	{
		using namespace lmms::ai;
		auto rev = AiProjectRevision::instance();

		QString clipHandle = "clp_1_10";
		QCOMPARE(rev->currentClipRevision(clipHandle), static_cast<uint64_t>(1));

		rev->bumpClipRevision(clipHandle);
		QCOMPARE(rev->currentClipRevision(clipHandle), static_cast<uint64_t>(2));

		rev->bumpClipRevision(clipHandle);
		QCOMPARE(rev->currentClipRevision(clipHandle), static_cast<uint64_t>(3));

		// Another clip is independent
		QCOMPARE(rev->currentClipRevision("clp_1_20"), static_cast<uint64_t>(1));
	}

	void testProjectReset()
	{
		using namespace lmms::ai;
		auto rev = AiProjectRevision::instance();
		rev->bumpRevision("e1");
		rev->bumpRevision("e2");

		rev->resetProject("new_project_id");
		QCOMPARE(rev->projectId(), QString("new_project_id"));
		QCOMPARE(rev->currentRevision(), static_cast<uint64_t>(1));
	}
};

QTEST_GUILESS_MAIN(AiProjectRevisionTest)
#include "AiProjectRevisionTest.moc"
