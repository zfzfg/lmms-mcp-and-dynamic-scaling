#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <QProcess>
#include <QLockFile>
#include <QThread>

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
    void claimedRecoveryCannotBeDiscardedByAnotherProcess()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("recover-race.mmp");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("session"); file.close();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--claim-child", path});
        QVERIFY(child.waitForStarted());
        QTRY_VERIFY(QFileInfo::exists(path + ".ready"));
        QString error;
        QVERIFY(!RecoveryIdentity::tryClaim(path, RecoveryIdentity::currentProcess(), &error));
        const bool removed = RecoveryIdentity::discardRecoveryFile(path, dir.path());
        QVERIFY(!removed);
        QVERIFY(QFileInfo::exists(path));
        child.kill(); QVERIFY(child.waitForFinished(5000));
        QVERIFY(RecoveryIdentity::tryClaim(path, RecoveryIdentity::currentProcess(), &error));
        RecoveryIdentity::releaseClaim(path);
        QVERIFY(!QFileInfo::exists(path + ".claim"));
    }
    void claimGuardSerializesDeletionAndRestore()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("recover-guard.mmp");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("session"); file.close();
        QLockFile deleting(path + ".claim-guard"); deleting.setStaleLockTime(0);
        QVERIFY(deleting.tryLock());
        QString error;
        QVERIFY(!RecoveryIdentity::tryClaim(path, RecoveryIdentity::currentProcess(), &error));
        QVERIFY(QFileInfo::exists(path));
    }
    void unreadableClaimAndLiveSessionAreConservative()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("recover-unknown.mmp");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("session"); file.close();
        QFile claim(path + ".claim"); QVERIFY(claim.open(QIODevice::WriteOnly)); claim.write("broken"); claim.close();
        QCOMPARE(RecoveryIdentity::claimLiveness(path), ProcessLiveness::Unknown);
        QString error;
        QVERIFY(!RecoveryIdentity::tryClaim(path, RecoveryIdentity::currentProcess(), &error));
        QVERIFY(QFile::remove(claim.fileName()));
        QVERIFY(RecoveryIdentity::writeLockFile(dir.filePath("recover-unknown.lock"), RecoveryIdentity::currentProcess(), &error));
        QVERIFY(!RecoveryIdentity::tryClaim(path, RecoveryIdentity::currentProcess(), &error));
    }

};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--claim-child")
    {
        QString error;
        if (!RecoveryIdentity::tryClaim(args[2], RecoveryIdentity::currentProcess(), &error)) { return 2; }
        QFile ready(args[2] + ".ready"); if (!ready.open(QIODevice::WriteOnly)) { return 3; } ready.close();
        for (;;) { QThread::msleep(10); }
    }
    RecoveryIdentityTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "RecoveryIdentityTest.moc"
