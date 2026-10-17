#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QtTest>

#ifdef _WIN32
#include "loader_paths.h"
#include <windows.h>
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
    void environmentAndArgumentsSurviveTheLauncher()
    {
#ifdef _WIN32
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("with spaces")));
        const QString child = dir.filePath("with spaces/child.exe");
        QVERIFY(QFile::copy(QString::fromUtf8(LMMS_CHILD_PATH), child));
        QVERIFY(QFile::copy(QString::fromUtf8(LMMS_DEP_DLL), QFileInfo(child).absolutePath() + '/' + QFileInfo(QString::fromUtf8(LMMS_DEP_DLL)).fileName()));
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("QT_PLUGIN_PATH"); env.insert("LMMS_ENV_SENTINEL", "preserved");
        QProcess process; process.setProcessEnvironment(env);
        const QStringList arguments{"space value", "quote\"value", "C:\\path with spaces\\", ""};
        QStringList command{"--plugins", "C:\\plugin path\\", "--timeout", "5", "--", child, "--inspect"};
        command.append(arguments);
        process.start(QString::fromUtf8(LMMS_LAUNCHER_PATH), command);
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
        const auto output = QString::fromLocal8Bit(process.readAllStandardOutput()).replace("\r\n", "\n");
        QVERIFY2(output.contains("plugins=C:\\plugin path\\\nsentinel=preserved\n"), qPrintable(output));
        for (const auto& argument : arguments) { QVERIFY2(output.contains("arg=" + argument + '\n'), qPrintable(output)); }
#endif
    }
    void timeoutKillsTheDescendantTree()
    {
#ifdef _WIN32
        QProcess process;
        process.start(QString::fromUtf8(LMMS_LAUNCHER_PATH), {"--timeout", "1", "--", QString::fromUtf8(LMMS_CHILD_PATH), "--tree"});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 124);
        const auto output = process.readAllStandardOutput();
        const auto pid = output.mid(output.indexOf("descendant=") + 11).trimmed().toULong();
        QVERIFY2(pid != 0, output.constData());
        HANDLE descendant = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (descendant)
        {
            const auto waited = WaitForSingleObject(descendant, 3000);
            CloseHandle(descendant);
            QCOMPARE(waited, static_cast<DWORD>(WAIT_OBJECT_0));
        }
        else { QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER)); }
#endif
    }

};

QTEST_GUILESS_MAIN(WindowsLoaderTest)
#include "WindowsLoaderTest.moc"
