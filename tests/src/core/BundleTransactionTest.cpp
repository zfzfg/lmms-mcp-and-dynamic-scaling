#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <cstdlib>

#include "BundleTransaction.h"

using namespace lmms;

class BundleTransactionTest : public QObject
{
	Q_OBJECT
private slots:
	void init()
	{
		BundleTransaction::setHooks(nullptr);
	}

	void cleanup()
	{
		BundleTransaction::setHooks(nullptr);
	}

	void publishesNewBundle()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bundle = dir.filePath("mix.v1");
		const auto result = BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("first"));
		QVERIFY(result.ok);
		QVERIFY(QFileInfo::exists(bundle + "/mix.v1.mmp"));
		QVERIFY(QFileInfo::exists(bundle + "/resources/tone.wav"));
		QVERIFY(!QFileInfo::exists(bundle + ".staging-"));
	}

	void foreignDirectoryIsNotReplaced()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bundle = dir.filePath("mix.v1");
		QVERIFY(QDir().mkpath(bundle));
		QFile note(bundle + "/notes.txt");
		QVERIFY(note.open(QIODevice::WriteOnly));
		note.write("keep");
		note.close();
		const auto result = BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("new"));
		QVERIFY(!result.ok);
		QCOMPARE(result.code, QStringLiteral("save.bundle.foreign"));
		QFile kept(bundle + "/notes.txt");
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("keep"));
	}

	void secondPublishKeepsALoadableBundle()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bundle = dir.filePath("mix.v1");
		QVERIFY(BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("one")).ok);
		const auto again = BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("two"));
		QVERIFY(again.ok);
		QFile project(bundle + "/mix.v1.mmp");
		QVERIFY(project.open(QIODevice::ReadOnly));
		QVERIFY(project.readAll().contains("two"));
	}

	void failedMoveLeavesTheOriginal()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bundle = dir.filePath("mix.v1");
		QVERIFY(BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("one")).ok);
		BundleIoHooks hooks;
		hooks.failAt = BundleIoHooks::Step::MoveOld;
		BundleTransaction::setHooks(&hooks);
		const auto result = BundleTransaction::publish(bundle, QStringLiteral("mix.v1.mmp"), copySample(), writeProject("two"));
		QVERIFY(!result.ok);
		QFile project(bundle + "/mix.v1.mmp");
		QVERIFY(project.open(QIODevice::ReadOnly));
		QVERIFY(project.readAll().contains("one"));
	}

    void projectNameAloneDoesNotAuthorizeReplacement()
    {
        QTemporaryDir dir;
        const QString bundle = dir.filePath("mix");
        QVERIFY(QDir().mkpath(bundle + "/resources"));
        QFile file(bundle + "/mix.mmp");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<lmms-project/>"); file.close();
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new")).ok);
        QVERIFY(QFileInfo::exists(file.fileName()));
        QFile manifest(bundle + "/.lmms-bundle");
        QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write("invalid"); manifest.close();
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new")).ok);
        QVERIFY(QFileInfo::exists(file.fileName()));
    }
    void invalidStagingAndManifestFailureKeepOldBundle()
    {
        QTemporaryDir dir;
        const QString bundle = dir.filePath("mix");
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("old")).ok);
        auto invalid = [](const QString& path, QString*) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write("broken XML") > 0;
        };
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), invalid).ok);
        BundleIoHooks hooks;
        hooks.failAt = BundleIoHooks::Step::WriteManifest;
        BundleTransaction::setHooks(&hooks);
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new")).ok);
        QFile old(bundle + "/mix.mmp"); QVERIFY(old.open(QIODevice::ReadOnly));
        QVERIFY(old.readAll().contains("old"));
    }
    void eachPreparationFailureKeepsThePreviousBundle_data()
    {
        QTest::addColumn<int>("step");
        for (const auto step : {BundleIoHooks::Step::CreateStaging, BundleIoHooks::Step::CopyResources,
            BundleIoHooks::Step::WriteProject, BundleIoHooks::Step::WriteManifest,
            BundleIoHooks::Step::MoveOld, BundleIoHooks::Step::PublishNew})
        {
            QTest::newRow(qPrintable(QString::number(static_cast<int>(step)))) << static_cast<int>(step);
        }
    }
    void eachPreparationFailureKeepsThePreviousBundle()
    {
        QFETCH(int, step);
        QTemporaryDir dir; const QString bundle = dir.filePath("mix");
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("old")).ok);
        BundleIoHooks hooks; hooks.failAt = static_cast<BundleIoHooks::Step>(step);
        BundleTransaction::setHooks(&hooks);
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new")).ok);
        QVERIFY(BundleTransaction::isIdentifiedBundle(bundle, "mix.mmp"));
        QFile project(bundle + "/mix.mmp"); QVERIFY(project.open(QIODevice::ReadOnly));
        QVERIFY(project.readAll().contains("old"));
    }
    void rollbackFailurePreservesBothTreesAndCanBeRecovered()
    {
        QTemporaryDir dir; const QString bundle = dir.filePath("mix");
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("old")).ok);
        BundleIoHooks hooks; hooks.failAt = BundleIoHooks::Step::PublishNew;
        hooks.observe = [&](BundleIoHooks::Step step) {
            if (step == BundleIoHooks::Step::RollbackOld) { hooks.failAt = step; hooks.seen = 0; }
        };
        BundleTransaction::setHooks(&hooks);
        const auto failed = BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new"));
        QVERIFY(!failed.ok); QVERIFY(!failed.restored);
        QVERIFY(BundleTransaction::isIdentifiedBundle(failed.backupPath, "mix.mmp"));
        QVERIFY(BundleTransaction::isIdentifiedBundle(failed.stagingPath, "mix.mmp"));
        BundleTransaction::setHooks(nullptr);
        BundleTransaction::recoverAbandoned(dir.path());
        QVERIFY(BundleTransaction::isIdentifiedBundle(bundle, "mix.mmp"));
    }
    void cleanupFailureIsSuccessAndNextPublishResolvesTheJournal()
    {
        QTemporaryDir dir; const QString bundle = dir.filePath("mix");
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("old")).ok);
        BundleIoHooks hooks; hooks.failAt = BundleIoHooks::Step::CleanupOld;
        BundleTransaction::setHooks(&hooks);
        const auto published = BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("new"));
        QVERIFY(published.ok); QVERIFY(published.cleanupWarning);
        QVERIFY(BundleTransaction::isIdentifiedBundle(published.backupPath, "mix.mmp"));
        BundleTransaction::setHooks(nullptr);
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("next")).ok);
    }
    void missingResourceFailsStagingValidation()
    {
        QTemporaryDir dir; const QString bundle = dir.filePath("mix");
        const auto invalid = [](const QString& path, QString*) {
            QFile file(path);
            const QByteArray bytes = "<lmms-project><sampleclip src='local:resources/missing.wav'/></lmms-project>";
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(!BundleTransaction::publish(bundle, "mix.mmp", copySample(), invalid).ok);
        QVERIFY(!QFileInfo::exists(bundle));
    }
    void twoProcessesCannotPublishTheSameTarget()
    {
        QTemporaryDir dir;
        const QString bundle = dir.filePath("mix");
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--bundle-child", bundle, "hold"});
        QVERIFY(child.waitForStarted());
        QTRY_VERIFY(QFileInfo::exists(bundle + ".ready"));
        const auto refused = BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("parent"));
        QVERIFY(!refused.ok);
        QCOMPARE(refused.code, QStringLiteral("save.bundle.busy"));
        // A different destination does not share the target lock.
        QVERIFY(BundleTransaction::publish(dir.filePath("other"), "other.mmp", copySample(), writeProject("other")).ok);
        QFile release(bundle + ".release"); QVERIFY(release.open(QIODevice::WriteOnly)); release.close();
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 0);
        QFile project(bundle + "/mix.mmp"); QVERIFY(project.open(QIODevice::ReadOnly));
        QVERIFY(project.readAll().contains("child"));
    }
    void crashRecovery_data()
    {
        QTest::addColumn<QString>("phase");
        for (const auto* phase : {"Prepared", "OldMoveIntent", "OldMoved", "NewPublished", "CleanupComplete"})
        {
            QTest::newRow(phase) << QString::fromLatin1(phase);
        }
    }
    void crashRecovery()
    {
        QFETCH(QString, phase);
        QTemporaryDir dir;
        const QString bundle = dir.filePath("mix");
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("old")).ok);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--bundle-child", bundle, phase});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 77);
        BundleTransaction::recoverAbandoned(dir.path());
        QVERIFY(BundleTransaction::isIdentifiedBundle(bundle, "mix.mmp"));
        QFile project(bundle + "/mix.mmp"); QVERIFY(project.open(QIODevice::ReadOnly));
        const auto bytes = project.readAll();
        project.close();
        QVERIFY(bytes.contains(phase == "Prepared" || phase == "OldMoveIntent" || phase == "OldMoved" ? "old" : "child"));
        QVERIFY(!QFileInfo::exists(bundle + ".bundle-transaction.json"));
        // A dead process lock can be acquired for another complete transaction.
        QVERIFY(BundleTransaction::publish(bundle, "mix.mmp", copySample(), writeProject("next")).ok);
    }

private:
	static BundleTransaction::CopyResources copySample()
	{
		return [](const QString& resourcesDir, QString*) {
			QFile sample(resourcesDir + "/tone.wav");
			if (!sample.open(QIODevice::WriteOnly)) { return false; }
			sample.write("wav");
			return true;
		};
	}

	static BundleTransaction::WriteProject writeProject(const char* marker)
	{
		const QByteArray body = QByteArray("<lmms-project>") + marker + "</lmms-project>";
		return [body](const QString& projectPath, QString*) {
			QFile file(projectPath);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
			return file.write(body) == body.size();
		};
	}
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--bundle-child")
    {
        const QString bundle = args[2], phase = args[3];
        BundleIoHooks hooks;
        hooks.observe = [&](BundleIoHooks::Step step) {
            if ((phase == "Prepared" && step == BundleIoHooks::Step::PreparedCheckpoint)
                || (phase == "OldMoveIntent" && step == BundleIoHooks::Step::OldMoveIntentCheckpoint)
                || (phase == "OldMoved" && step == BundleIoHooks::Step::OldMovedCheckpoint)
                || (phase == "NewPublished" && step == BundleIoHooks::Step::NewPublishedCheckpoint)
                || (phase == "CleanupComplete" && step == BundleIoHooks::Step::CleanupCheckpoint)) { std::_Exit(77); }
        };
        BundleTransaction::setHooks(&hooks);
        const auto result = BundleTransaction::publish(bundle, "mix.mmp",
            [&](const QString& resources, QString*) {
                if (phase == "hold")
                {
                    QFile ready(bundle + ".ready"); if (!ready.open(QIODevice::WriteOnly)) { return false; } ready.close();
                    QElapsedTimer timer; timer.start();
                    while (!QFileInfo::exists(bundle + ".release") && timer.elapsed() < 5000) { QThread::msleep(5); }
                }
                QFile file(resources + "/tone.wav"); return file.open(QIODevice::WriteOnly) && file.write("wav") == 3;
            }, [](const QString& path, QString*) {
                QFile file(path); const QByteArray xml = "<!DOCTYPE lmms-project><lmms-project>child</lmms-project>";
                return file.open(QIODevice::WriteOnly) && file.write(xml) == xml.size();
            });
        return result.ok ? 0 : 1;
    }
    BundleTransactionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BundleTransactionTest.moc"
