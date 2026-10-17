#include "AiCommandDispatcher.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QDir>
#include <QLocalSocket>
#include <QtEndian>
#include <QtTest>

#include "AiControlService.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include "DataFile.h"
#include "Song.h"

namespace {
struct ScopedProcess : QProcess
{
	~ScopedProcess() override
	{
		if (state() != QProcess::NotRunning) { kill(); waitForFinished(1000); }
	}
};
}

class AiCommandDispatcherTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_directory;
private slots:
	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
		QVERIFY(lmms::ai::AiControlService::instance()->startService(m_directory.filePath("connection.json")));
		lmms::ai::AiControlService::instance()->setReady(true);
		QTRY_VERIFY2(lmms::ai::AiControlService::instance()->isReady(),
			qPrintable(lmms::ai::AiControlService::instance()->lastError()));
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void testPermissionCheck()
	{
		using namespace lmms::ai;
		auto dispatcher = AiCommandDispatcher::instance();

		QJsonObject payload;
		bool callbackInvoked = false;
		QJsonObject responseReceived;

		// Dispatch write action with only Read permission
		dispatcher->dispatch(
			1, "apply_changes", payload, "op_perm_1", 1000, Permission::Read, [&](const QJsonObject& res) {
				callbackInvoked = true;
				responseReceived = res;
			});

		QVERIFY(callbackInvoked);
		QVERIFY(!responseReceived["ok"].toBool());
		QCOMPARE(responseReceived["error"].toObject()["code"].toString(), QString("PERMISSION_DENIED"));
	}

	void testDeadlineTimeout()
	{
		using namespace lmms::ai;
		auto dispatcher = AiCommandDispatcher::instance();

		QJsonObject payload;
		bool callbackInvoked = false;
		QJsonObject responseReceived;

		// Dispatch with negative / expired deadline
		dispatcher->dispatch(2, "get_status", payload, "", -100, Permission::Read, [&](const QJsonObject& res) {
			callbackInvoked = true;
			responseReceived = res;
		});

		QCoreApplication::processEvents();

		QVERIFY(callbackInvoked);
		QVERIFY(!responseReceived["ok"].toBool());
		QCOMPARE(responseReceived["error"].toObject()["code"].toString(), QString("TIMEOUT"));
	}
	void testConcurrentRepeatedOperationHasOneResult()
	{
		using namespace lmms::ai;
		auto* dispatcher = AiCommandDispatcher::instance();
		const QJsonObject payload{{"projectId", AiProjectRevision::instance()->projectId()},
			{"expectedRevision", static_cast<qint64>(AiProjectRevision::instance()->currentRevision())},
			{"action", "stop"}};
		int replies = 0;
		QJsonObject first, second;
		dispatcher->dispatch(10, "set_transport", payload, "same-key", 1000,
			Permission::Read | Permission::WriteProject, [&](const QJsonObject& result) {
				first = result;
				++replies;
			});
		dispatcher->dispatch(11, "set_transport", payload, "same-key", 1000,
			Permission::Read | Permission::WriteProject, [&](const QJsonObject& result) {
				second = result;
				++replies;
			});
		QTRY_COMPARE(replies, 2);
		QVERIFY(first["ok"].toBool());
		QCOMPARE(first, second);
		QJsonObject registered;
		QVERIFY(dispatcher->lookupOperation("same-key", registered));
		QCOMPARE(registered, first);
	}
	void testPermissionRevokedWhileQueued()
	{
		using namespace lmms::ai;
		auto* service = AiControlService::instance();
		auto* dispatcher = AiCommandDispatcher::instance();
		bool replied = false;
		QJsonObject response;
		dispatcher->dispatch(12, "set_transport",
			{{"action", "stop"}, {"projectId", AiProjectRevision::instance()->projectId()},
				{"expectedRevision", static_cast<qint64>(AiProjectRevision::instance()->currentRevision())}},
			"revoked-key", 1000, Permission::Read | Permission::WriteProject, [&](const QJsonObject& result) {
				response = result;
				replied = true;
			});
		service->setGrantedPermissions(Permission::Read);
		QTRY_VERIFY(replied);
		QVERIFY(!response["ok"].toBool());
		QCOMPARE(response["error"].toObject()["code"].toString(), QString("PERMISSION_DENIED"));
		service->setGrantedPermissions(
			Permission::Read | Permission::WriteProject | Permission::Export | Permission::Assets);
	}
	void testDisconnectedQueuedCommandIsCancelled()
	{
		using namespace lmms::ai;
		bool replied = false;
		QJsonObject response;
		AiCommandDispatcher::instance()->dispatch(
			13, "set_transport",
			{{"action", "stop"}, {"projectId", AiProjectRevision::instance()->projectId()},
				{"expectedRevision", static_cast<qint64>(AiProjectRevision::instance()->currentRevision())}},
			"disconnected-key", 1000, Permission::Read | Permission::WriteProject,
			[&](const QJsonObject& result) {
				response = result;
				replied = true;
			},
			[] { return false; });
		QTRY_VERIFY(replied);
		QCOMPARE(response["error"].toObject()["code"].toString(), QString("CANCELLED"));
	}
	void testNodeClientAgainstRealQtService()
	{
		const auto node = QStandardPaths::findExecutable("node");
		QVERIFY2(!node.isEmpty(), "MCP integration testing requires the build-time Node runtime.");
		const auto peer = QFINDTESTDATA("../../../tools/mcp/dist/test/native-peer.js");
		QVERIFY2(!peer.isEmpty(), "Build mcp-companion before running native integration tests.");
		QProcess process;
		process.start(node, {peer, lmms::ai::AiControlService::instance()->connectionFilePath()});
		QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 15000);
		const auto errors = process.readAllStandardError();
		QVERIFY2(process.exitCode() == 0, errors.constData());
		QVERIFY(QJsonDocument::fromJson(process.readAllStandardOutput()).object()["ok"].toBool());
	}
	void testRapidRestartIgnoresPreviousStartCompletion()
	{
		using namespace lmms::ai;
		auto* service = AiControlService::instance();
		QSignalSpy changes(service, &AiControlService::statusChanged);
		service->stopService();
		QVERIFY(service->startService(m_directory.filePath("first.json")));
		const auto first = service->instanceId();
		service->stopService();
		QVERIFY(service->startService(m_directory.filePath("second.json")));
		QVERIFY(service->instanceId() != first);
		QVERIFY(!service->isPublished());
		QTRY_VERIFY(service->isReady());
		QVERIFY(!QFile::exists(m_directory.filePath("first.json")));
		QFile file(service->connectionFilePath());
		QVERIFY(file.open(QIODevice::ReadOnly));
		const auto published = QJsonDocument::fromJson(file.readAll()).object();
		QCOMPARE(published["instanceId"].toString(), service->instanceId());
		QCOMPARE(published["endpoint"].toString(), service->endpointName());
		QVERIFY(changes.count() >= 4);
	}
	void testInstrumentsInIsolatedApplication()
	{
#ifndef LMMS_TEST_APP
		QSKIP("The application integration test requires Organic and TripleOscillator in PLUGIN_LIST.");
#else
		// Windows instrument DLLs import lmms.exe; they must run in the real app,
		// not in a second copy of the engine linked into this test executable.
		QTemporaryDir directory;
		QVERIFY(QDir(directory.path()).mkpath("presets"));
		lmms::DataFile preset(lmms::DataFile::Type::InstrumentTrackSettings);
		auto track = preset.createElement("instrumenttrack");
		preset.content().appendChild(track);
		auto instrument = preset.createElement("instrument");
		instrument.setAttribute("name", "organic");
		instrument.appendChild(preset.createElement("organic"));
		track.appendChild(instrument);
		QVERIFY(preset.writeFile(directory.filePath("presets/test-organic.xpf"), false, false));
		QFile malformed(directory.filePath("presets/malformed.xpf"));
		QVERIFY(malformed.open(QIODevice::WriteOnly));
		malformed.write("<incomplete"); malformed.close();
		QFile config(directory.filePath("config.xml"));
		QVERIFY(config.open(QIODevice::WriteOnly));
		QByteArray xml = "<lmms version=\"1.3.0-alpha\" configversion=\"1\"><app configured=\"1\" language=\"en\" openlastproject=\"0\"/>"
			"<ui enableautosave=\"0\"/><audioengine audiodev=\"Dummy (no sound output)\" mididev=\"Dummy (no MIDI support)\"/>"
			"<paths workingdir=\"";
		xml += directory.path().toHtmlEscaped().toUtf8();
		xml += "/\"/></lmms>";
		config.write(xml); config.close();
		ScopedProcess app;
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert("QT_QPA_PLATFORM", "offscreen");
		environment.insert("LMMS_DATA_DIR", LMMS_TEST_DATA_DIR);
		app.setProcessEnvironment(environment);
		const auto connection = directory.filePath("connection.json");
		QStringList arguments{"--config", config.fileName(), "--mcp-connection-file", connection};
		const auto debugger = qEnvironmentVariable("LMMS_TEST_DEBUGGER");
		if (!debugger.isEmpty()) { arguments.prepend(LMMS_TEST_APP); }
		app.start(debugger.isEmpty() ? LMMS_TEST_APP : debugger, arguments);
		QVERIFY(app.waitForStarted());
		QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(connection) || app.state() == QProcess::NotRunning, 10000);
		QVERIFY2(QFile::exists(connection), app.readAllStandardError().constData());
		const auto peer = QFINDTESTDATA("../../../tools/mcp/dist/test/native-peer.js");
		ScopedProcess node;
		node.start(QStandardPaths::findExecutable("node"), {peer, connection, "--instruments"});
		QTRY_VERIFY_WITH_TIMEOUT(node.state() == QProcess::NotRunning, 10000);
		const auto errors = node.readAllStandardError();
		QVERIFY2(node.exitCode() == 0,
			(errors + app.readAllStandardError() + app.readAllStandardOutput()).constData());
		QVERIFY2(app.state() == QProcess::Running, app.readAllStandardError().constData());
		app.kill();
		QVERIFY(app.waitForFinished());
#endif
	}
	void testPublicationFailureKeepsForeignFileAndCanRecover()
	{
		using namespace lmms::ai;
		auto* service = AiControlService::instance();
		service->stopService();
		const auto occupied = m_directory.filePath("occupied.json");
		QFile file(occupied);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write("foreign file"), qint64(12));
		file.close();
		QVERIFY(service->startService(occupied));
		QVERIFY(!service->isPublished());
		QTRY_VERIFY(!service->isRunning());
		QVERIFY(!service->isReady());
		QVERIFY(!service->lastError().isEmpty());
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("foreign file"));
		file.close();
		QVERIFY(service->startService(m_directory.filePath("recovered.json")));
		QTRY_VERIFY(service->isReady());
		QVERIFY(service->lastError().isEmpty());
	}
	void testPermissionChangeRevokesAuthenticatedSocket()
	{
		using namespace lmms::ai;
		auto* service = AiControlService::instance();
		QLocalSocket socket;
		socket.connectToServer(service->endpointName());
		QVERIFY(socket.waitForConnected(1000));
		const QJsonObject request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "auth.handshake"},
			{"params", QJsonObject{{"instanceId", service->instanceId()}, {"authToken", service->authToken()},
				{"ipcProtocolVersion", "1.0"}}}};
		const auto body = QJsonDocument(request).toJson(QJsonDocument::Compact);
		QByteArray header(4, '\0');
		qToBigEndian<quint32>(body.size(), reinterpret_cast<uchar*>(header.data()));
		socket.write(header + body);
		QVERIFY(socket.waitForReadyRead(1000));
		QByteArray response = socket.readAll();
		while (response.size() < 4 || response.size() < 4 + qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(response.constData())))
		{
			QVERIFY(socket.waitForReadyRead(1000));
			response += socket.readAll();
		}
		QVERIFY(QJsonDocument::fromJson(response.mid(4)).object()["result"].toObject()["ok"].toBool());
		const auto previous = service->grantedPermissions();
		service->setGrantedPermissions(Permission::Read);
		QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
		service->setGrantedPermissions(previous);
		QVERIFY(service->isReady());
	}
};

QTEST_GUILESS_MAIN(AiCommandDispatcherTest)
#include "AiCommandDispatcherTest.moc"
