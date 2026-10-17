#include "AiEditTransaction.h"

#include <QtTest>

#include "AiApplicationFacade.h"
#include "AiChangePlan.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

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

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::ai::AiProjectRevision::instance()->resetProject("proj_test");
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
		QCOMPARE(t->numOfClips(), 0);
		Engine::projectJournal()->redo();
		QCOMPARE(song->getTempo(), 125);
		QCOMPARE(t->numOfClips(), 1);
		QCOMPARE(dynamic_cast<MidiClip*>(t->getClips().front())->notes().size(), std::size_t(2));
	}
	void testFailureAfterEachMutationRestoresHistory_data()
	{
		QTest::addColumn<int>("failAfter");
		for (int step = 1; step <= 3; ++step)
		{
			QTest::newRow(qPrintable(QString::number(step))) << step;
		}
	}
	void testFailureAfterEachMutationRestoresHistory()
	{
		using namespace lmms;
		using namespace lmms::ai;
		QFETCH(int, failAfter);
		auto* song = Engine::getSong();
		auto* journal = Engine::projectJournal();
		auto* track = Track::create(Track::Type::Instrument, song);
		journal->addJournalCheckPoint(&song->tempoModel());
		song->setTempo(140);
		journal->undo();
		const auto undo = journal->currentUndoGroupId(), redo = journal->currentRedoGroupId();
		const int tempo = song->getTempo();
		song->setModified(false);
		const auto handle = AiObjectRegistry::instance()->registerTrack(track);
		const auto revision = AiProjectRevision::instance()->currentRevision();
		ChangeCommand first;
		first.kind = "set_tempo";
		first.bpm = 125;
		ChangeCommand second;
		second.kind = "create_midi_clip";
		second.trackId = handle;
		second.clientRef = "new";
		second.positionTicks = 0;
		second.lengthTicks = 192;
		ChangeCommand third;
		third.kind = "add_notes";
		third.targetClipClientRef = "new";
		NoteData note;
		note.positionTicks = 0;
		note.lengthTicks = 48;
		note.midiKey = 60;
		third.notes.push_back(note);
		auto plan = std::make_shared<AiChangePlan>(
			"fault", "proj_test", revision, std::vector<ChangeCommand>{first, second, third});
		const auto result = AiEditTransaction::executePlan(song, plan, "fault", 0, [failAfter](int completed) {
			if (completed == failAfter) { throw DomainError{ErrorCode::InternalError, "Injected failure"}; }
		});
		QVERIFY(!result.ok);
		QCOMPARE(song->getTempo(), tempo);
		QVERIFY(track->getClips().empty());
		QVERIFY(!song->isModified());
		QCOMPARE(AiProjectRevision::instance()->currentRevision(), revision);
		QCOMPARE(journal->currentUndoGroupId(), undo);
		QCOMPARE(journal->currentRedoGroupId(), redo);
	}

	void testInvalidLaterCommandLeavesProjectAndHistoryUntouched()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		auto* journal = Engine::projectJournal();
		const int tempo = song->getTempo();
		const auto revision = AiProjectRevision::instance()->currentRevision();
		const auto undo = journal->currentUndoGroupId(), redo = journal->currentRedoGroupId();
		ChangeCommand first;
		first.kind = "set_tempo";
		first.bpm = 160;
		ChangeCommand bad;
		bad.kind = "set_mixer_send";
		bad.mixerChannelIndex = -1;
		auto plan
			= std::make_shared<AiChangePlan>("invalid", "proj_test", revision, std::vector<ChangeCommand>{first, bad});
		auto result = AiEditTransaction::executePlan(song, plan, "invalid-operation");
		QVERIFY(!result.ok);
		QCOMPARE(song->getTempo(), tempo);
		QCOMPARE(journal->currentUndoGroupId(), undo);
		QCOMPARE(journal->currentRedoGroupId(), redo);
		QCOMPARE(AiProjectRevision::instance()->currentRevision(), revision);
	}
	void testCancelGroupRestoresModelAndPreservesRedo()
	{
		using namespace lmms;
		auto* song = Engine::getSong();
		auto* journal = Engine::projectJournal();
		song->setTempo(120);
		journal->clearJournal();
		journal->addJournalCheckPoint(&song->tempoModel());
		song->setTempo(130);
		journal->undo();
		const auto redo = journal->currentRedoGroupId();
		QVERIFY(!redo.isEmpty());
		journal->beginJournalGroup("failing", "failing");
		journal->addJournalCheckPoint(&song->tempoModel());
		song->setTempo(140);
		journal->cancelJournalGroup();
		QCOMPARE(song->getTempo(), 120);
		QCOMPARE(journal->currentRedoGroupId(), redo);
		journal->redo();
		QCOMPARE(song->getTempo(), 130);
	}
	void testManualNoteChangeInvalidatesPreviewAndNoteRefs()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		const auto id = AiObjectRegistry::instance()->registerClip(clip);
		const auto before = AiProjectRevision::instance()->currentRevision();
		const auto clipBefore = AiProjectRevision::instance()->currentClipRevision(id);
		ChangeCommand command;
		command.kind = "transpose_notes";
		command.targetClipId = id;
		command.transposeSemitones = 12;
		auto plan = std::make_shared<AiChangePlan>("stale", "proj_test", before, std::vector<ChangeCommand>{command});
		clip->notes().front()->setKey(61);
		song->setModified(); // Piano Roll edit boundary, even when already dirty.
		QVERIFY(AiProjectRevision::instance()->currentClipRevision(id) != clipBefore);
		auto result = AiEditTransaction::executePlan(song, plan, "stale");
		QVERIFY(!result.ok);
		QCOMPARE(result.error.code, ErrorCode::RevisionConflict);
		QCOMPARE(clip->notes().front()->key(), 61);
	}
	void testNoOpHasNoHistoryOrRevision()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		const auto before = AiProjectRevision::instance()->currentRevision();
		const auto head = Engine::projectJournal()->currentUndoGroupId();
		ChangeCommand command;
		command.kind = "set_tempo";
		command.bpm = song->getTempo();
		auto plan = std::make_shared<AiChangePlan>("noop", "proj_test", before, std::vector<ChangeCommand>{command});
		auto result = AiEditTransaction::executePlan(song, plan, "noop");
		QVERIFY(result.ok);
		QCOMPARE(result.status, QString("unchanged"));
		QCOMPARE(AiProjectRevision::instance()->currentRevision(), before);
		QCOMPARE(Engine::projectJournal()->currentUndoGroupId(), head);
	}
	void testMixedNoteUpdateAndRemoval()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(48), TimePos(48), 64), false);
		auto* registry = AiObjectRegistry::instance();
		const auto id = registry->registerClip(clip);
		const auto rev = AiProjectRevision::instance()->currentRevision();
		const auto clipRev = AiProjectRevision::instance()->currentClipRevision(id);
		ChangeCommand update;
		update.kind = "update_notes";
		update.targetClipId = id;
		NoteData note;
		note.noteRef = registry->generateNoteRef(id, clipRev, 0);
		note.midiKey = 67;
		note.lengthTicks = 48;
		update.notes.push_back(note);
		ChangeCommand remove;
		remove.kind = "remove_notes";
		remove.targetClipId = id;
		remove.noteRefs << registry->generateNoteRef(id, clipRev, 1);
		auto plan
			= std::make_shared<AiChangePlan>("notes", "proj_test", rev, std::vector<ChangeCommand>{update, remove});
		auto result = AiEditTransaction::executePlan(song, plan, "notes");
		QVERIFY(result.ok);
		QCOMPARE(clip->notes().size(), std::size_t(1));
		QCOMPARE(clip->notes().front()->key(), 67);
		Engine::projectJournal()->undo();
		clip = dynamic_cast<MidiClip*>(track->getClips().front());
		QCOMPARE(clip->notes().size(), std::size_t(2));
		QCOMPARE(clip->notes().front()->key(), 60);
	}

	void testNoteUpdatePreservesUnspecifiedDynamics()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		auto* track = Track::create(Track::Type::Instrument, song);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60, 73, 24), false);
		const auto handle = AiObjectRegistry::instance()->registerClip(clip);
		const auto revision = AiProjectRevision::instance()->currentRevision();
		const auto ref = AiObjectRegistry::instance()->generateNoteRef(
			handle, AiProjectRevision::instance()->currentClipRevision(handle), 0);
		DomainError error;
		const auto command = ChangeCommand::fromJson(
			QJsonObject{{"kind", "update_notes"}, {"clipId", handle},
				{"notes",
					QJsonArray{
						QJsonObject{{"noteRef", ref}, {"positionTicks", 12}, {"lengthTicks", 24}, {"midiKey", 62}}}}},
			error);
		QVERIFY(!error.hasError());
		auto plan
			= std::make_shared<AiChangePlan>("dynamics", "proj_test", revision, std::vector<ChangeCommand>{command});
		const auto result = AiEditTransaction::executePlan(song, plan, "dynamics");
		QVERIFY(result.ok);
		QCOMPARE(clip->notes().front()->getVolume(), 73);
		QCOMPARE(clip->notes().front()->getPanning(), 24);
		QCOMPARE(clip->notes().front()->key(), 62);
	}

	void testSongAndPatternStoreScopesRestoreInBothDirections()
	{
		using namespace lmms;
		using namespace lmms::ai;
		auto* song = Engine::getSong();
		auto* store = Engine::patternStore();
		Track::create(Track::Type::Pattern, song);
		auto* ordinary = Track::create(Track::Type::Instrument, song);
		auto* pattern = Track::create(Track::Type::Instrument, store);
		pattern->setName("original pattern");
		const auto ordinaryId = AiObjectRegistry::instance()->registerTrack(ordinary);
		const auto patternId = AiObjectRegistry::instance()->registerTrack(pattern);
		ChangeCommand rename;
		rename.kind = "rename_track";
		rename.trackId = patternId;
		rename.newName = "edited pattern";
		ChangeCommand remove;
		remove.kind = "delete_track";
		remove.trackId = ordinaryId;
		const auto revision = AiProjectRevision::instance()->currentRevision();
		auto plan = std::make_shared<AiChangePlan>(
			"owners", "proj_test", revision, std::vector<ChangeCommand>{rename, remove});
		QVERIFY(AiEditTransaction::executePlan(song, plan, "owners").ok);
		QCOMPARE(store->tracks().front()->name(), QString("edited pattern"));
		QCOMPARE(song->tracks().size(), std::size_t(1));
		Engine::projectJournal()->undo();
		QCOMPARE(store->tracks().front()->name(), QString("original pattern"));
		QCOMPARE(song->tracks().size(), std::size_t(2));
		Engine::projectJournal()->redo();
		QCOMPARE(store->tracks().front()->name(), QString("edited pattern"));
		QCOMPARE(song->tracks().size(), std::size_t(1));
	}
};

QTEST_GUILESS_MAIN(AiEditTransactionTest)
#include "AiEditTransactionTest.moc"
