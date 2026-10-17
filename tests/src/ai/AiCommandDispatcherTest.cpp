#include "AiCommandDispatcher.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "AiControlService.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include "Song.h"

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
		QTRY_VERIFY(lmms::ai::AiControlService::instance()->isReady());
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
};

QTEST_GUILESS_MAIN(AiCommandDispatcherTest)
#include "AiCommandDispatcherTest.moc"
