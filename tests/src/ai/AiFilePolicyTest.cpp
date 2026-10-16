#include <QtTest>
#include "AiFilePolicy.h"
#include <QTemporaryDir>
#include <QFile>

class AiFilePolicyTest : public QObject
{
	Q_OBJECT
private slots:
	void testSafeFileName()
	{
		using namespace lmms::ai;
		auto policy = AiFilePolicy::instance();

		QVERIFY(policy->isSafeFileName("melody.wav"));
		QVERIFY(policy->isSafeFileName("my_cool_project_2026.mmp"));

		// Traversal
		QVERIFY(!policy->isSafeFileName("../escape.mmp"));
		QVERIFY(!policy->isSafeFileName("sub/dir.mmp"));
		QVERIFY(!policy->isSafeFileName("C:\\windows\\win.ini"));

		// Windows device names
		QVERIFY(!policy->isSafeFileName("CON.wav"));
		QVERIFY(!policy->isSafeFileName("con.txt"));
		QVERIFY(!policy->isSafeFileName("aux.mmp"));
		QVERIFY(!policy->isSafeFileName("NUL"));
		QVERIFY(!policy->isSafeFileName("COM1.wav"));

		// Alternate Data Streams
		QVERIFY(!policy->isSafeFileName("file.txt:hidden"));
	}

	void testPathResolutionAndConfinement()
	{
		using namespace lmms::ai;
		auto policy = AiFilePolicy::instance();

		QTemporaryDir tempRoot;
		QVERIFY(tempRoot.isValid());

		policy->setAllowedDirectory(AiFilePolicy::RootType::Exports, tempRoot.path());

		QString resolved;
		DomainError err = policy->validateAndResolve(AiFilePolicy::RootType::Exports, "out.wav", resolved, false);
		QVERIFY(!err.hasError());
		QVERIFY(resolved.contains("out.wav"));

		// Test traversal attempt
		DomainError errTrav = policy->validateAndResolve(AiFilePolicy::RootType::Exports, "../outside.wav", resolved, false);
		QVERIFY(errTrav.hasError());
		QCOMPARE(errTrav.code, ErrorCode::PathNotAllowed);
	}
};

QTEST_GUILESS_MAIN(AiFilePolicyTest)
#include "AiFilePolicyTest.moc"
