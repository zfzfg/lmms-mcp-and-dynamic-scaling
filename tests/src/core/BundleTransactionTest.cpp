#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

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

QTEST_GUILESS_MAIN(BundleTransactionTest)
#include "BundleTransactionTest.moc"
