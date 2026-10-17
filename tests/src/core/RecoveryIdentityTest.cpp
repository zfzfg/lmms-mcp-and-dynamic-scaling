#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "RecoveryIdentity.h"

using namespace lmms;

class RecoveryIdentityTest : public QObject
{
	Q_OBJECT
private slots:
	void currentProcessIsAlive()
	{
		const auto self = RecoveryIdentity::currentProcess();
		QVERIFY(self.pid > 0);
		QCOMPARE(RecoveryIdentity::inspect(self), ProcessLiveness::Alive);
	}

	void roundTripLockAndLegacyPid()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("recover.lock");
		auto identity = RecoveryIdentity::currentProcess();
		identity.uuid = QStringLiteral("abc");
		QString error;
		QVERIFY(RecoveryIdentity::writeLockFile(path, identity, &error));
		const auto read = RecoveryIdentity::readLockFile(path);
		QVERIFY(read.parsed);
		QCOMPARE(read.version, 2);
		QCOMPARE(read.identity.pid, identity.pid);
		QCOMPARE(read.identity.startToken, identity.startToken);
		QCOMPARE(RecoveryIdentity::inspect(read.identity), ProcessLiveness::Alive);

		QFile legacy(dir.filePath("old.lock"));
		QVERIFY(legacy.open(QIODevice::WriteOnly));
		legacy.write("999999\nlegacy\n");
		legacy.close();
		const auto old = RecoveryIdentity::readLockFile(legacy.fileName());
		QVERIFY(old.legacyPidOnly);
		QCOMPARE(old.identity.pid, static_cast<qint64>(999999));
	}

	void rejectsPathsOutsideTheRecoveryDirectory()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString outside = QDir(dir.path()).filePath("../recover.mmp");
		QFile file(QFileInfo(outside).absoluteFilePath());
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("x");
		file.close();
		QVERIFY(!RecoveryIdentity::isDeletableRecoveryFile(file.fileName(), dir.path()));
		const QString inside = dir.filePath("recover-session.mmp");
		QFile good(inside);
		QVERIFY(good.open(QIODevice::WriteOnly));
		good.write("x");
		good.close();
		QVERIFY(RecoveryIdentity::isDeletableRecoveryFile(inside, dir.path()));
		QVERIFY(!RecoveryIdentity::isDeletableRecoveryFile(dir.filePath("notes.mmp"), dir.path()));
	}

	void secondClaimDoesNotStealALiveOwner()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recovery = dir.filePath("recover-one.mmp");
		QFile file(recovery);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("session");
		file.close();
		auto who = RecoveryIdentity::currentProcess();
		QString error;
		QVERIFY(RecoveryIdentity::tryClaim(recovery, who, &error));
		QString secondError;
		QVERIFY(!RecoveryIdentity::tryClaim(recovery, who, &secondError));
		QVERIFY(secondError.contains(QStringLiteral("claimed")) || secondError.contains(QStringLiteral("untouched")));
		RecoveryIdentity::releaseClaim(recovery);
		QVERIFY(RecoveryIdentity::tryClaim(recovery, who, &error));
		RecoveryIdentity::releaseClaim(recovery);
	}
};

QTEST_GUILESS_MAIN(RecoveryIdentityTest)
#include "RecoveryIdentityTest.moc"
