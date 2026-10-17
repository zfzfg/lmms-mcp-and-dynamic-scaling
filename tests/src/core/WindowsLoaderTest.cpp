#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QtTest>

#ifdef _WIN32
#include "loader_paths.h"
#endif

class WindowsLoaderTest : public QObject
{
	Q_OBJECT
private slots:
	void missingDllFailsWithoutWaitingForADialog()
	{
#ifndef _WIN32
		QSKIP("Windows loader reproduction");
#else
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString childCopy = dir.filePath(QStringLiteral("child.exe"));
		QVERIFY(QFile::copy(QString::fromUtf8(LMMS_CHILD_PATH), childCopy));
		QProcess process;
		process.start(QString::fromUtf8(LMMS_LAUNCHER_PATH), {QStringLiteral("--timeout"), QStringLiteral("15"), QStringLiteral("--"), childCopy});
		QElapsedTimer timer;
		timer.start();
		QVERIFY(process.waitForFinished(15000));
		QVERIFY(timer.elapsed() < 15000);
		QVERIFY(process.exitCode() != 0);
#endif
	}

	void presentDllExitsZero()
	{
#ifndef _WIN32
		QSKIP("Windows loader reproduction");
#else
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString childCopy = dir.filePath(QStringLiteral("child.exe"));
		const QString dllCopy = dir.filePath(QFileInfo(QString::fromUtf8(LMMS_DEP_DLL)).fileName());
		QVERIFY(QFile::copy(QString::fromUtf8(LMMS_CHILD_PATH), childCopy));
		QVERIFY(QFile::copy(QString::fromUtf8(LMMS_DEP_DLL), dllCopy));
		QProcess process;
		process.start(QString::fromUtf8(LMMS_LAUNCHER_PATH), {QStringLiteral("--timeout"), QStringLiteral("15"), QStringLiteral("--"), childCopy});
		QVERIFY(process.waitForFinished(15000));
		QCOMPARE(process.exitCode(), 0);
#endif
	}
};

QTEST_GUILESS_MAIN(WindowsLoaderTest)
#include "WindowsLoaderTest.moc"
