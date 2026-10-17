#include <QtTest>
#include <QTemporaryDir>
#include <QLabel>
#include <QPushButton>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTranslator>
#include "AiControlService.h"
#include "Engine.h"
#include "../../../src/gui/modals/AiConnectionDialog.h"

class AiConnectionDialogTest : public QObject
{
	Q_OBJECT
private slots:
	void testEnglishStatusAndPublishedConfiguration()
	{
		using namespace lmms;
		using namespace lmms::ai;
		QTemporaryDir directory;
		Engine::init(true);
		auto* service = AiControlService::instance();
		gui::AiConnectionDialog dialog;
		QCOMPARE(dialog.windowTitle(), QString("AI Control / MCP Connection"));
		QPushButton* copy = nullptr;
		QPushButton* toggle = nullptr;
		for (auto* button : dialog.findChildren<QPushButton*>())
		{
			if (button->text() == "Copy Host Configuration") { copy = button; }
			if (button->text() == "Start Service") { toggle = button; }
		}
		QVERIFY(copy);
		QVERIFY(toggle);
		QVERIFY(!copy->isEnabled());
		service->setReady(false);
		QVERIFY(service->startService(directory.filePath("connection.json")));
		QVERIFY(!copy->isEnabled());
		QTRY_VERIFY(service->isPublished());
		QVERIFY(copy->isEnabled());
		QVERIFY(!service->isReady());
		auto status = [&] {
			for (auto* label : dialog.findChildren<QLabel*>())
				if (label->text().startsWith("<b>Status:")) { return label->text(); }
			return QString();
		};
		QVERIFY(status().contains("Starting"));
		service->setReady(true);
		QVERIFY(status().contains("Ready"));
		copy->click();
		const auto config = QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
		const auto args = config["mcpServers"].toObject()["lmms"].toObject()["args"].toArray();
		QCOMPARE(args.at(0).toString(), QString("--connection-directory"));
		QCOMPARE(args.at(1).toString(), QFileInfo(service->connectionFilePath()).absolutePath());
		toggle->click();
		QVERIFY(status().contains("Stopped"));
		QVERIFY(!copy->isEnabled());
		// The dialog must not mark an unready project ready when starting again.
		service->setReady(false);
		toggle->click();
		QTRY_VERIFY(service->isPublished());
		QVERIFY(!service->isReady());
		service->stopService();
		QFile occupied(directory.filePath("occupied.json"));
		QVERIFY(occupied.open(QIODevice::WriteOnly));
		occupied.write("occupied"); occupied.close();
		QVERIFY(service->startService(occupied.fileName()));
		QTRY_VERIFY(!service->isRunning());
		QVERIFY(status().contains("Failed"));
		QVERIFY(!copy->isEnabled());
		#ifdef LMMS_TEST_TRANSLATION_FILE
		QTranslator translator;
		QVERIFY(translator.load(LMMS_TEST_TRANSLATION_FILE));
		QVERIFY(QApplication::installTranslator(&translator));
		gui::AiConnectionDialog translated;
		QCOMPARE(translated.windowTitle(), QString::fromUtf8("KI-Steuerung / MCP-Verbindung"));
		QApplication::removeTranslator(&translator);
		#endif
		Engine::destroy();
	}
};

QTEST_MAIN(AiConnectionDialogTest)
#include "AiConnectionDialogTest.moc"
