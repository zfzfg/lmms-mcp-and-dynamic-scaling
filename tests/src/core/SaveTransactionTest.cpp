#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

#include "SaveTransaction.h"

using namespace lmms;

class SaveTransactionTest : public QObject
{
	Q_OBJECT
private slots:
	void init()
	{
		SaveTransaction::setHooks(nullptr);
	}

	void cleanup()
	{
		SaveTransaction::setHooks(nullptr);
	}

	void successReplacesProjectAndKeepsPreviousAsBackup()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QVERIFY(writeBytes(path, "A"));
		QVERIFY(writeBytes(path + ".bak", "B"));
		const auto result = SaveTransaction::publish(path, true, writer("C"));
		QVERIFY(result.ok);
		QCOMPARE(readBytes(path), QByteArray("C"));
		QCOMPARE(readBytes(path + ".bak"), QByteArray("A"));
		QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("song.mmp.bak.*")}).size(), 0);
	}

	void failedCopyLeavesBothGenerations()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QVERIFY(writeBytes(path, "A"));
		QVERIFY(writeBytes(path + ".bak", "B"));
		SaveIoHooks hooks;
		hooks.failAt = SaveIoHooks::Step::CopyProjectToCandidate;
		SaveTransaction::setHooks(&hooks);
		const auto result = SaveTransaction::publish(path, true, writer("C"));
		QVERIFY(!result.ok);
		QCOMPARE(readBytes(path), QByteArray("A"));
		QCOMPARE(readBytes(path + ".bak"), QByteArray("B"));
	}

	void failedCommitRestoresPreviousBackup()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QVERIFY(writeBytes(path, "A"));
		QVERIFY(writeBytes(path + ".bak", "B"));
		SaveIoHooks hooks;
		hooks.failAt = SaveIoHooks::Step::Commit;
		SaveTransaction::setHooks(&hooks);
		const auto result = SaveTransaction::publish(path, true, writer("C"));
		QVERIFY(!result.ok);
		QCOMPARE(result.code, QStringLiteral("save.commit"));
		QCOMPARE(readBytes(path), QByteArray("A"));
		QCOMPARE(readBytes(path + ".bak"), QByteArray("B"));
	}

	void failedCleanupKeepsPreviousGenerationAndReportsSuccess()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QVERIFY(writeBytes(path, "A"));
		QVERIFY(writeBytes(path + ".bak", "B"));
		SaveIoHooks hooks;
		hooks.failAt = SaveIoHooks::Step::CleanupPrevious;
		SaveTransaction::setHooks(&hooks);
		const auto result = SaveTransaction::publish(path, true, writer("C"));
		QVERIFY(result.ok);
		QVERIFY(result.cleanupWarning);
		QCOMPARE(readBytes(path), QByteArray("C"));
		QCOMPARE(readBytes(path + ".bak"), QByteArray("A"));
		QVERIFY(!result.artifacts.isEmpty());
	}

	void missingBackupIsNotInventedWhenCommitFails()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QVERIFY(writeBytes(path, "A"));
		SaveIoHooks hooks;
		hooks.failAt = SaveIoHooks::Step::Commit;
		SaveTransaction::setHooks(&hooks);
		const auto result = SaveTransaction::publish(path, true, writer("C"));
		QVERIFY(!result.ok);
		QCOMPARE(readBytes(path), QByteArray("A"));
		QVERIFY(!QFileInfo::exists(path + ".bak"));
	}

private:
	static bool writeBytes(const QString& path, const QByteArray& bytes)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
		return file.write(bytes) == bytes.size();
	}

	static QByteArray readBytes(const QString& path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) { return {}; }
		return file.readAll();
	}

	static SaveTransaction::Writer writer(const char* text)
	{
		const QByteArray bytes(text);
		return [bytes](QSaveFile& file, QString*) { return file.write(bytes) == bytes.size(); };
	}
};

QTEST_GUILESS_MAIN(SaveTransactionTest)
#include "SaveTransactionTest.moc"
