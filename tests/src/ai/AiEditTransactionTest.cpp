#include <QtTest>
#include "AiEditTransaction.h"
#include "AiChangePlan.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "ProjectJournal.h"

class AiEditTransactionTest : public QObject
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

	void testRevisionConflictRejection()
	{
		using namespace lmms;
		using namespace lmms::ai;

		Song* song = Engine::getSong();
		uint64_t actualRev = AiProjectRevision::instance()->currentRevision();

		std::vector<ChangeCommand> cmds;
		ChangeCommand c;
		c.kind = "set_tempo";
		c.bpm = 130;
		cmds.push_back(c);

		// Expect older revision (actualRev - 1)
		auto plan = std::make_shared<AiChangePlan>("plan_conflict", "proj_test", actualRev - 1, cmds);
		ApplyResult res = AiEditTransaction::executePlan(song, plan, "op_conf_1");

		QVERIFY(!res.ok);
		QCOMPARE(res.error.code, ErrorCode::RevisionConflict);
	}

	void testAtomicCommitAndUndo()
	{
		using namespace lmms;
		using namespace lmms::ai;

		Song* song = Engine::getSong();
		uint64_t initialRev = AiProjectRevision::instance()->currentRevision();

		// Create a test track
		Track* t = Track::create(Track::Type::Instrument, song);
		QString trackHandle = AiObjectRegistry::instance()->registerTrack(t);
		AiProjectRevision::instance()->bumpRevision("track_added");
		uint64_t currentRev = AiProjectRevision::instance()->currentRevision();

		// Build change plan: set tempo to 125, create clip, add notes
		std::vector<ChangeCommand> cmds;

		ChangeCommand cmdTempo;
		cmdTempo.kind = "set_tempo";
		cmdTempo.bpm = 125;
		cmds.push_back(cmdTempo);

		ChangeCommand cmdClip;
		cmdClip.kind = "create_midi_clip";
		cmdClip.trackId = trackHandle;
		cmdClip.clientRef = "test_clip_1";
		cmdClip.positionTicks = 0;
		cmdClip.lengthTicks = 192;
		cmds.push_back(cmdClip);

		ChangeCommand cmdNotes;
		cmdNotes.kind = "add_notes";
		cmdNotes.targetClipClientRef = "test_clip_1";
		NoteData n1;
		n1.midiKey = 60;
		n1.positionTicks = 0;
		n1.lengthTicks = 48;
		cmdNotes.notes.push_back(n1);

		NoteData n2;
		n2.midiKey = 64;
		n2.positionTicks = 48;
		n2.lengthTicks = 48;
		cmdNotes.notes.push_back(n2);
		cmds.push_back(cmdNotes);

		auto plan = std::make_shared<AiChangePlan>("plan_batch", "proj_test", currentRev, cmds);
		ApplyResult res = AiEditTransaction::executePlan(song, plan, "op_batch_1");

		QVERIFY(res.ok);
		QCOMPARE(res.status, QString("committed"));
		QCOMPARE(res.addedNotes, 2);
		QCOMPARE(song->getTempo(), 125);
		QVERIFY(res.newRevision > currentRev);

		// Single undo must undo both the tempo change, the clip creation, and the notes
		Engine::projectJournal()->undo();
		QVERIFY(song->getTempo() != 125);
	}
};

QTEST_GUILESS_MAIN(AiEditTransactionTest)
#include "AiEditTransactionTest.moc"
