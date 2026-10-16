#include <QtTest>
#include "AiCommandDispatcher.h"
#include "Engine.h"

class AiCommandDispatcherTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
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
		dispatcher->dispatch(1, "apply_changes", payload, "op_perm_1", 1000, Permission::Read,
			[&](const QJsonObject& res)
			{
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
		dispatcher->dispatch(2, "get_status", payload, "", -100, Permission::Read,
			[&](const QJsonObject& res)
			{
				callbackInvoked = true;
				responseReceived = res;
			});

		QCoreApplication::processEvents();

		QVERIFY(callbackInvoked);
		QVERIFY(!responseReceived["ok"].toBool());
		QCOMPARE(responseReceived["error"].toObject()["code"].toString(), QString("TIMEOUT"));
	}
};

QTEST_GUILESS_MAIN(AiCommandDispatcherTest)
#include "AiCommandDispatcherTest.moc"
