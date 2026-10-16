#include "AiFilePolicy.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>

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
		DomainError errTrav
			= policy->validateAndResolve(AiFilePolicy::RootType::Exports, "../outside.wav", resolved, false);
		QVERIFY(errTrav.hasError());
		QCOMPARE(errTrav.code, ErrorCode::PathNotAllowed);
	}
	void testSiblingPrefixAndExistingDestinationAreRejected()
	{
		using namespace lmms::ai;
		QTemporaryDir temp;
		QVERIFY(QDir(temp.path()).mkdir("exports"));
		QVERIFY(QDir(temp.path()).mkdir("exports-other"));
		auto* policy = AiFilePolicy::instance();
		policy->setAllowedDirectory(AiFilePolicy::RootType::Exports, temp.path() + "/exports");
		QVERIFY(!policy->isPathAllowed(temp.path() + "/exports-other/new.wav", AiFilePolicy::RootType::Exports));
		QFile file(temp.path() + "/exports/existing.wav");
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("keep");
		file.close();
		QString resolved;
		QVERIFY(
			policy->validateAndResolve(AiFilePolicy::RootType::Exports, "existing.wav", resolved, false).hasError());
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("keep"));
	}
	void testWindowsSpecialNames()
	{
		auto* policy = lmms::ai::AiFilePolicy::instance();
		for (const auto& name : {"CON.foo.wav", "file.", "file ", "a?b.wav", "a*b.wav", "a|b.wav"})
		{
			QVERIFY(!policy->isSafeFileName(name));
		}
	}

	void testPublishesNewFileWithoutOverwriting()
	{
		using namespace lmms::ai;
		QTemporaryDir root;
		QFile source(root.filePath("source"));
		QVERIFY(source.open(QIODevice::WriteOnly));
		source.write("complete artifact");
		source.close();
		QVERIFY(QDir(root.path()).mkdir("exports"));
		auto* policy = AiFilePolicy::instance();
		policy->setAllowedDirectory(AiFilePolicy::RootType::Exports, root.filePath("exports"));
		QString result;
		QVERIFY(!policy->publishNewFile(source.fileName(), "new.wav", result).hasError());
		QFile published(result);
		QVERIFY(published.open(QIODevice::ReadOnly));
		QCOMPARE(published.readAll(), QByteArray("complete artifact"));
		published.close();
		QVERIFY(policy->publishNewFile(source.fileName(), "new.wav", result).hasError());
	}
	void testReplacedRootIsRejected()
	{
		using namespace lmms::ai;
		QTemporaryDir temp;
		QDir directory(temp.path());
		QVERIFY(directory.mkdir("exports"));
		QVERIFY(directory.mkdir("outside"));
		auto* policy = AiFilePolicy::instance();
		policy->setAllowedDirectory(AiFilePolicy::RootType::Exports, temp.filePath("exports"));
		QVERIFY(directory.rename("exports", "original"));
		std::error_code error;
		std::filesystem::create_directory_symlink(std::filesystem::path(temp.filePath("outside").toStdWString()),
			std::filesystem::path(temp.filePath("exports").toStdWString()), error);
		if (error) { QSKIP("Platform does not permit test symlink creation."); }
		QString resolved;
		QVERIFY(policy->validateAndResolve(AiFilePolicy::RootType::Exports, "outside.wav", resolved, false).hasError());
		QVERIFY(!QFile::exists(temp.filePath("outside/outside.wav")));
	}
};

QTEST_GUILESS_MAIN(AiFilePolicyTest)
#include "AiFilePolicyTest.moc"
