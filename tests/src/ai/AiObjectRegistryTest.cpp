#include <QtTest>
#include "AiObjectRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"

class AiObjectRegistryTest : public QObject
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

	void testNoteRefParsing()
	{
		using namespace lmms::ai;
		auto reg = AiObjectRegistry::instance();
		QString noteRef = reg->generateNoteRef("clp_1_2", 42, 7);
		QCOMPARE(noteRef, QString("nt_clp_1_2_42_7"));

		QString clipHandle;
		uint64_t clipRev = 0;
		int noteIndex = -1;
		bool ok = reg->parseNoteRef(noteRef, clipHandle, clipRev, noteIndex);
		QVERIFY(ok);
		QCOMPARE(clipHandle, QString("clp_1_2"));
		QCOMPARE(clipRev, static_cast<uint64_t>(42));
		QCOMPARE(noteIndex, 7);

		// Invalid ref
		bool bad = reg->parseNoteRef("invalid_ref", clipHandle, clipRev, noteIndex);
		QVERIFY(!bad);
	}

	void testTrackRegistrationAndLookup()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto reg = AiObjectRegistry::instance();

		Track* t = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(t != nullptr);

		QString handle = reg->registerTrack(t);
		QVERIFY(!handle.isEmpty());
		QVERIFY(handle.startsWith("trk_"));

		Track* found = reg->lookupTrack(handle);
		QCOMPARE(found, t);

		QString foundHandle = reg->findTrackHandle(t);
		QCOMPARE(foundHandle, handle);

		// Non-existent handle
		Track* notFound = reg->lookupTrack("trk_9999_9999");
		QVERIFY(notFound == nullptr);
	}

	void testMixerChannelRegistration()
	{
		using namespace lmms::ai;
		auto reg = AiObjectRegistry::instance();

		QString handle = reg->registerMixerChannel(5);
		QVERIFY(!handle.isEmpty());
		QVERIFY(handle.startsWith("mix_"));

		int idx = reg->lookupMixerChannel(handle);
		QCOMPARE(idx, 5);

		// Same channel returns same handle
		QString handleAgain = reg->registerMixerChannel(5);
		QCOMPARE(handleAgain, handle);
	}

	void testInvalidation()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto reg = AiObjectRegistry::instance();

		Track* t = Track::create(Track::Type::Instrument, Engine::getSong());
		QString handle = reg->registerTrack(t);
		uint64_t genBefore = reg->currentGeneration();

		reg->invalidateAll();
		uint64_t genAfter = reg->currentGeneration();
		QVERIFY(genAfter > genBefore);

		// Old handle must now return nullptr
		Track* found = reg->lookupTrack(handle);
		QVERIFY(found == nullptr);
	}
};

QTEST_GUILESS_MAIN(AiObjectRegistryTest)
#include "AiObjectRegistryTest.moc"
