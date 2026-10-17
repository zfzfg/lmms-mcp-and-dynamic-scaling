#include <QtTest>

#include "AiChangePlan.h"
class AiChangePlanValidationTest : public QObject
{
	Q_OBJECT
private slots:
	void malformedNumbers_data()
	{
		QTest::addColumn<QByteArray>("json");
		QTest::newRow("string tempo") << QByteArray(R"({"kind":"set_tempo","bpm":"120"})");
		QTest::newRow("fraction tempo") << QByteArray(R"({"kind":"set_tempo","bpm":120.5})");
		QTest::newRow("missing mute") << QByteArray(R"({"kind":"set_track_mute","trackId":"t"})");
		QTest::newRow("overflow end") << QByteArray(
			R"({"kind":"create_midi_clip","trackId":"t","clientRef":"c","positionTicks":2147483647,"lengthTicks":48})");
		QTest::newRow("fraction note") << QByteArray(
			R"({"kind":"add_notes","clip":{"clipId":"c"},"notes":[{"positionTicks":0,"lengthTicks":48,"midiKey":60.5}]})");
		QTest::newRow("pan bounds") << QByteArray(
			R"({"kind":"add_notes","clip":{"clipId":"c"},"notes":[{"positionTicks":0,"lengthTicks":48,"midiKey":60,"pan":101}]})");
		QTest::newRow("empty remove") << QByteArray(R"({"kind":"remove_notes","clipId":"c","noteRefs":[]})");
	}
	void malformedNumbers()
	{
		QFETCH(QByteArray, json);
		lmms::ai::DomainError error;
		lmms::ai::ChangeCommand::fromJson(QJsonDocument::fromJson(json).object(), error);
		QVERIFY(error.hasError());
	}
	void validNote()
	{
		lmms::ai::DomainError error;
		auto command = lmms::ai::ChangeCommand::fromJson(
			QJsonDocument::fromJson(
				R"({"kind":"add_notes","clip":{"clipId":"c"},"notes":[{"positionTicks":0,"lengthTicks":48,"midiKey":60}]})")
				.object(),
			error);
		QVERIFY(!error.hasError());
		QCOMPARE(command.notes.size(), std::size_t(1));
	}
};
QTEST_GUILESS_MAIN(AiChangePlanValidationTest)
#include "AiChangePlanValidationTest.moc"
