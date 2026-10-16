#include "AiEditTransaction.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "AiParameterRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "AudioEngine.h"
#include <QUuid>

namespace lmms::ai
{

ApplyResult AiEditTransaction::executePlan(Song* song,
                                          std::shared_ptr<AiChangePlan> plan,
                                          const QString& operationKey)
{
	Q_UNUSED(operationKey);
	ApplyResult res;

	if (!song)
	{
		res.error.code = ErrorCode::NotReady;
		res.error.message = "Song is not available.";
		return res;
	}

	if (!plan)
	{
		res.error.code = ErrorCode::ValidationError;
		res.error.message = "Null change plan provided.";
		return res;
	}

	if (plan->isExpired())
	{
		res.error.code = ErrorCode::PlanExpired;
		res.error.message = "Change plan has expired (5-minute lifetime exceeded).";
		return res;
	}

	// 1. Revision check
	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (currentRev != plan->expectedRevision())
	{
		res.error.code = ErrorCode::RevisionConflict;
		res.error.message = QString("Project revision conflict: expected %1 but current is %2.")
			.arg(plan->expectedRevision()).arg(currentRev);
		QJsonObject details;
		details["expectedRevision"] = static_cast<qint64>(plan->expectedRevision());
		details["actualRevision"] = static_cast<qint64>(currentRev);
		res.error.details = details;
		return res;
	}

	// 2. Playback check
	if (song->isPlaying())
	{
		res.error.code = ErrorCode::PlaybackActive;
		res.error.message = "Structural edits and note modifications cannot be performed while playback is active.";
		return res;
	}

	QString undoGroupId = QString("undo_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	ProjectJournal* journal = Engine::projectJournal();
	journal->beginJournalGroup(plan->summary(), undoGroupId);

	QHash<QString, MidiClip*> createdClipsByClientRef;
	QJsonObject createdObjects;
	int totalAddedNotes = 0;
	bool success = true;

	{
		// RAII audio thread change lock
		auto guard = Engine::audioEngine()->requestChangesGuard();

		for (const auto& cmd : plan->commands())
		{
			if (cmd.kind == "set_tempo")
			{
				journal->addJournalCheckPoint(&song->tempoModel());
				song->tempoModel().setValue(cmd.bpm);
			}
			else if (cmd.kind == "rename_track")
			{
				Track* t = AiObjectRegistry::instance()->lookupTrack(cmd.trackId);
				if (!t)
				{
					res.error.code = ErrorCode::StaleHandle;
					res.error.message = QString("Track handle not found: %1").arg(cmd.trackId);
					success = false;
					break;
				}
				journal->addJournalCheckPoint(t);
				t->setName(cmd.newName);
			}
			else if (cmd.kind == "set_track_mute")
			{
				Track* t = AiObjectRegistry::instance()->lookupTrack(cmd.trackId);
				if (!t)
				{
					res.error.code = ErrorCode::StaleHandle;
					res.error.message = QString("Track handle not found: %1").arg(cmd.trackId);
					success = false;
					break;
				}
				journal->addJournalCheckPoint(t);
				t->setMuted(cmd.muted);
			}
			else if (cmd.kind == "set_parameter")
			{
				DomainError paramErr = AiParameterRegistry::instance()->setParameterValue(cmd.parameterId, cmd.paramValue);
				if (paramErr.hasError())
				{
					res.error = paramErr;
					success = false;
					break;
				}
			}
			else if (cmd.kind == "create_midi_clip")
			{
				Track* t = AiObjectRegistry::instance()->lookupTrack(cmd.trackId);
				if (!t)
				{
					res.error.code = ErrorCode::StaleHandle;
					res.error.message = QString("Track handle not found: %1").arg(cmd.trackId);
					success = false;
					break;
				}
				auto it = dynamic_cast<InstrumentTrack*>(t);
				if (!it)
				{
					res.error.code = ErrorCode::ValidationError;
					res.error.message = QString("Track '%1' is not an InstrumentTrack.").arg(cmd.trackId);
					success = false;
					break;
				}

				journal->addJournalCheckPoint(it);
				Clip* c = it->createClip(TimePos(cmd.positionTicks));
				auto mc = dynamic_cast<MidiClip*>(c);
				if (!mc)
				{
					res.error.code = ErrorCode::InternalError;
					res.error.message = "Created clip is not a MidiClip.";
					success = false;
					break;
				}

				mc->setLength(TimePos(cmd.lengthTicks));
				QString handle = AiObjectRegistry::instance()->registerClip(mc);
				createdClipsByClientRef.insert(cmd.clientRef, mc);
				createdObjects[cmd.clientRef] = handle;
			}
			else if (cmd.kind == "add_notes")
			{
				MidiClip* mc = nullptr;
				if (!cmd.targetClipClientRef.isEmpty() && createdClipsByClientRef.contains(cmd.targetClipClientRef))
				{
					mc = createdClipsByClientRef.value(cmd.targetClipClientRef);
				}
				else if (!cmd.targetClipId.isEmpty())
				{
					mc = AiObjectRegistry::instance()->lookupMidiClip(cmd.targetClipId);
				}

				if (!mc)
				{
					res.error.code = ErrorCode::StaleHandle;
					res.error.message = QString("Target clip not found: %1").arg(cmd.targetClipId);
					success = false;
					break;
				}

				journal->addJournalCheckPoint(mc);
				for (const auto& nd : cmd.notes)
				{
					Note n(TimePos(nd.lengthTicks), TimePos(nd.positionTicks), nd.midiKey, nd.volumePercent, nd.pan);
					if (nd.noteType == "step")
					{
						n.setType(Note::Type::Step);
					}
					else
					{
						n.setType(Note::Type::Regular);
					}
					mc->addNote(n, false /* do not quantize AI notes */);
					totalAddedNotes++;
				}
				mc->updateLength();
				AiProjectRevision::instance()->bumpClipRevision(AiObjectRegistry::instance()->findClipHandle(mc));
			}
			else if (cmd.kind == "transpose_notes")
			{
				MidiClip* mc = AiObjectRegistry::instance()->lookupMidiClip(cmd.targetClipId);
				if (!mc)
				{
					res.error.code = ErrorCode::StaleHandle;
					res.error.message = QString("Target clip not found: %1").arg(cmd.targetClipId);
					success = false;
					break;
				}

				// Check bounds for all notes first
				const auto& notes = mc->notes();
				for (const auto n : notes)
				{
					int targetKey = n->key() + cmd.transposeSemitones;
					if (targetKey < 0 || targetKey > 127)
					{
						res.error.code = ErrorCode::ValidationError;
						res.error.message = QString("Transposition would push note %1 to key %2 (outside 0..127).")
							.arg(n->key()).arg(targetKey);
						success = false;
						break;
					}
				}

				if (!success) { break; }

				journal->addJournalCheckPoint(mc);
				for (auto n : notes)
				{
					n->setKey(n->key() + cmd.transposeSemitones);
				}
				AiProjectRevision::instance()->bumpClipRevision(cmd.targetClipId);
			}
			else if (cmd.kind == "create_mixer_channel")
			{
				Engine::mixer()->createChannel();
			}
			else if (cmd.kind == "set_mixer_send")
			{
				Engine::mixer()->createChannelSend(cmd.mixerChannelIndex, cmd.mixerSendTargetIndex, cmd.mixerSendAmount);
			}
		}
	}

	if (!success)
	{
		journal->cancelJournalGroup();
		res.ok = false;
		res.status = "rejected";
		return res;
	}

	journal->endJournalGroup();
	song->setModified(true);
	AiProjectRevision::instance()->bumpRevision("edit_transaction_committed");

	res.ok = true;
	res.status = "committed";
	res.newRevision = AiProjectRevision::instance()->currentRevision();
	res.undoGroupId = undoGroupId;
	res.createdObjects = createdObjects;
	res.addedNotes = totalAddedNotes;

	return res;
}

} // namespace lmms::ai
