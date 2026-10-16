#include "AiEditTransaction.h"

#include <QSet>
#include <QUuid>
#include <cmath>
#include <limits>

#include "AiObjectRegistry.h"
#include "AiParameterRegistry.h"
#include "AiProjectRevision.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms::ai {

namespace {
Note* resolveNote(const QString& ref, MidiClip* clip)
{
	QString id;
	uint64_t revision;
	int index;
	auto* registry = AiObjectRegistry::instance();
	if (!clip || !registry->parseNoteRef(ref, id, revision, index) || registry->lookupMidiClip(id) != clip
		|| revision != AiProjectRevision::instance()->currentClipRevision(id) || index < 0
		|| index >= static_cast<int>(clip->notes().size()))
	{
		return nullptr;
	}
	return clip->notes()[index];
}
struct RevisionGuard
{
	bool changed{false};
	RevisionGuard() { AiProjectRevision::instance()->beginEdit(); }
	~RevisionGuard() { AiProjectRevision::instance()->endEdit(changed); }
};
} // namespace

DomainError AiEditTransaction::validateCommands(const std::vector<ChangeCommand>& commands)
{
	auto invalid = [](const QString& message) { return DomainError{ErrorCode::ValidationError, message}; };
	if (commands.empty() || commands.size() > MAX_BATCH_COMMANDS) { return invalid("Expected 1..64 commands."); }
	auto* registry = AiObjectRegistry::instance();
	QSet<QString> clientRefs;
	QSet<QString> midiClientRefs;
	QHash<MidiClip*, QList<int>> addedKeys;
	QSet<QString> deletedClips;
	QSet<Track*> deletedTracks;
	QSet<QString> usedNotes;
	QHash<Note*, int> keys;

	QHash<int, QSet<int>> routes;
	auto* mixer = Engine::mixer();
	int channels = mixer->numChannels();
	for (int i = 0; i < channels; ++i)
		for (auto* send : mixer->mixerChannel(i)->m_sends)
		{
			routes[i].insert(send->receiverIndex());
		}
	int count = 0;
	for (const auto& cmd : commands)
	{
		auto* genericClip = registry->lookupClip(cmd.targetClipId);
		if (deletedClips.contains(cmd.targetClipId) || deletedTracks.contains(registry->lookupTrack(cmd.trackId))
			|| (genericClip && deletedTracks.contains(genericClip->getTrack())))
		{
			return invalid("Object was deleted earlier in this plan.");
		}
		if (cmd.kind == "set_tempo")
		{
			if (Engine::getSong()->tempoModel().isAutomatedOrControlled())
			{
				return {ErrorCode::ParameterControlled, "Tempo is automated or controlled."};
			}
			if (cmd.bpm < MinTempo || cmd.bpm > MaxTempo) { return invalid("Tempo out of range."); }
		}
		else if (cmd.kind == "rename_track" || cmd.kind == "set_track_mute" || cmd.kind == "create_midi_clip")
		{
			auto* track = registry->lookupTrack(cmd.trackId);
			if (!track) { return {ErrorCode::StaleHandle, "Track no longer exists."}; }
			if (cmd.kind == "create_midi_clip")
			{
				if (!dynamic_cast<InstrumentTrack*>(track) || cmd.clientRef.isEmpty()
					|| clientRefs.contains(cmd.clientRef) || cmd.positionTicks < 0 || cmd.lengthTicks <= 0
					|| static_cast<qint64>(cmd.positionTicks) + cmd.lengthTicks > std::numeric_limits<tick_t>::max())
				{
					return invalid("Invalid clip or duplicate clientRef.");
				}
				clientRefs.insert(cmd.clientRef);
				midiClientRefs.insert(cmd.clientRef);
			}
		}
		else if (cmd.kind == "set_parameter")
		{
			ParameterInfo info;
			if (!AiParameterRegistry::instance()->getParameterInfo(cmd.parameterId, info))
			{
				return {ErrorCode::StaleHandle, "Unknown parameter."};
			}
			if (deletedTracks.contains(registry->lookupTrack(info.ownerId)))
			{
				return invalid("Parameter owner deleted earlier in plan.");
			}
			if (!info.isWritable || info.isAutomated)
			{
				return {ErrorCode::ParameterControlled, "Parameter is controlled or read-only."};
			}
			if (!std::isfinite(cmd.paramValue) || cmd.paramValue < info.minValue || cmd.paramValue > info.maxValue
				|| (info.step >= 1 && std::floor(cmd.paramValue) != cmd.paramValue))
			{
				return invalid("Invalid parameter value.");
			}
		}
		else if (cmd.kind == "add_notes" || cmd.kind == "update_notes" || cmd.kind == "remove_notes"
			|| cmd.kind == "transpose_notes")
		{
			auto* clip = registry->lookupMidiClip(cmd.targetClipId);
			if (!cmd.targetClipClientRef.isEmpty())
			{
				if (cmd.kind != "add_notes" || !cmd.targetClipId.isEmpty()
					|| !midiClientRefs.contains(cmd.targetClipClientRef))
				{
					return invalid("Invalid local clip reference.");
				}
			}
			else if (!clip) { return {ErrorCode::StaleHandle, "Clip no longer exists."}; }
			count += cmd.kind == "transpose_notes" ? static_cast<int>(clip->notes().size())
												   : static_cast<int>(cmd.notes.size() + cmd.noteRefs.size());
			if (count > static_cast<int>(MAX_BATCH_NOTES)) { return invalid("Batch exceeds 512 affected notes."); }
			if (cmd.kind == "transpose_notes")
			{
				for (auto* n : clip->notes())
				{
					if (!cmd.noteRefs.isEmpty()
						&& !cmd.noteRefs.contains(registry->generateNoteRef(cmd.targetClipId,
							AiProjectRevision::instance()->currentClipRevision(cmd.targetClipId),
							static_cast<int>(
								std::find(clip->notes().begin(), clip->notes().end(), n) - clip->notes().begin()))))
					{
						continue;
					}
					const qint64 target = static_cast<qint64>(keys.value(n, n->key())) + cmd.transposeSemitones;
					if (target < 0 || target > 127) { return invalid("Transposed key outside 0..127."); }
					keys[n] = static_cast<int>(target);
				}
				if (cmd.noteRefs.isEmpty())
					for (int& key : addedKeys[clip])
					{
						if (static_cast<qint64>(key) + cmd.transposeSemitones < 0
							|| static_cast<qint64>(key) + cmd.transposeSemitones > 127)
						{
							return invalid("Transposed added note out of range.");
						}
						key += cmd.transposeSemitones;
					}
			}
			for (const auto& n : cmd.notes)
			{
				if (cmd.kind == "add_notes" && clip) { addedKeys[clip].append(n.midiKey); }
				if (n.midiKey < 0 || n.midiKey > 127 || n.positionTicks < 0 || n.lengthTicks <= 0 || n.volumePercent < 0
					|| n.volumePercent > 200 || n.pan < -100 || n.pan > 100
					|| (n.noteType != "regular" && n.noteType != "step"))
				{
					return invalid("Invalid note values.");
				}
				if (cmd.kind == "update_notes")
				{
					auto* note = resolveNote(n.noteRef, clip);
					if (!note || usedNotes.contains(n.noteRef))
					{
						return {ErrorCode::StaleHandle, "Invalid or repeated note reference."};
					}
					usedNotes.insert(n.noteRef);
					keys[note] = n.midiKey;
				}
			}
			for (const auto& ref : cmd.noteRefs)
			{
				if (!resolveNote(ref, clip) || usedNotes.contains(ref))
				{
					return {ErrorCode::StaleHandle, "Invalid or repeated note reference."};
				}
				usedNotes.insert(ref);
			}
		}
		else if (cmd.kind == "create_mixer_channel")
		{
			routes[channels].insert(0);
			++channels;
		}
		else if (cmd.kind == "set_mixer_send")
		{
			int from = cmd.rawParams.contains("fromChannelId")
				? registry->lookupMixerChannel(cmd.rawParams["fromChannelId"].toString())
				: cmd.mixerChannelIndex;
			int to = cmd.rawParams.contains("toChannelId")
				? registry->lookupMixerChannel(cmd.rawParams["toChannelId"].toString())
				: cmd.mixerSendTargetIndex;
			if (from < 0 || to < 0 || from >= channels || to >= channels || from == to
				|| !std::isfinite(cmd.mixerSendAmount) || cmd.mixerSendAmount < 0 || cmd.mixerSendAmount > 1)
			{
				return invalid("Invalid mixer send.");
			}
			routes[from].insert(to);
		}

		else if (cmd.kind == "delete_track")
		{
			auto* track = registry->lookupTrack(cmd.trackId);
			if (!track) { return {ErrorCode::StaleHandle, "Unknown track."}; }
			deletedTracks.insert(track);
		}
		else if (cmd.kind == "assign_mixer_channel")
		{
			if (!dynamic_cast<InstrumentTrack*>(registry->lookupTrack(cmd.trackId))
				|| registry->lookupMixerChannel(cmd.rawParams["channelId"].toString()) < 0)
			{
				return invalid("Unknown instrument track or mixer channel.");
			}
		}
		else if (cmd.kind == "assign_pattern")
		{
			if (!dynamic_cast<PatternTrack*>(registry->lookupTrack(cmd.trackId)))
			{
				return invalid("patternId must identify a song PatternTrack.");
			}
		}
		else if (cmd.kind == "move_clip" || cmd.kind == "resize_clip" || cmd.kind == "duplicate_clip"
			|| cmd.kind == "delete_clip" || cmd.kind == "set_pattern_steps" || cmd.kind == "set_automation_points")
		{
			const bool newAutomation = cmd.kind == "set_automation_points" && cmd.targetClipId.isEmpty();
			if (!genericClip && !newAutomation) { return {ErrorCode::StaleHandle, "Unknown clip."}; }
			if (newAutomation && (cmd.clientRef.isEmpty() || clientRefs.contains(cmd.clientRef)))
			{
				return invalid("New automation requires a unique clientRef.");
			}
			if (newAutomation) { clientRefs.insert(cmd.clientRef); }
			const qint64 end = cmd.kind == "resize_clip"
				? static_cast<qint64>(genericClip->startPosition().getTicks()) + cmd.lengthTicks
				: static_cast<qint64>(cmd.positionTicks)
					+ (genericClip ? genericClip->length().getTicks() : cmd.lengthTicks);
			if (end > std::numeric_limits<tick_t>::max()) { return invalid("Clip end overflows native ticks."); }
			if (cmd.kind == "delete_clip") { deletedClips.insert(cmd.targetClipId); }
			if (cmd.kind == "duplicate_clip" && (cmd.clientRef.isEmpty() || clientRefs.contains(cmd.clientRef)))
			{
				return invalid("Duplicate clip requires a unique clientRef.");
			}
			if (cmd.kind == "duplicate_clip")
			{
				clientRefs.insert(cmd.clientRef);
				if (dynamic_cast<MidiClip*>(genericClip)) { midiClientRefs.insert(cmd.clientRef); }
			}
			if (cmd.kind == "resize_clip")
			{
				if (auto* midi = dynamic_cast<MidiClip*>(genericClip))
					for (auto* note : midi->notes())
						if (note->type() == Note::Type::Regular
							&& static_cast<qint64>(note->pos().getTicks()) + note->length().getTicks()
								> cmd.lengthTicks)
						{
							return invalid("Resize would truncate notes.");
						}
			}
			if (cmd.kind == "set_pattern_steps")
			{
				auto* midi = dynamic_cast<MidiClip*>(genericClip);
				if (!midi || !midi->isInPattern() || !cmd.rawParams["steps"].isArray())
				{
					return invalid("Steps require a PatternStore MIDI clip.");
				}
				auto steps = cmd.rawParams["steps"].toArray();
				count += steps.size();
				if (steps.isEmpty() || count > static_cast<int>(MAX_BATCH_NOTES))
				{
					return invalid("Invalid step batch size.");
				}
				for (const auto& value : steps)
				{
					auto step = value.toObject();
					auto index = step["stepIndex"];
					if (!index.isDouble() || index.toDouble() != index.toInt() || index.toInt(-1) < 0
						|| index.toInt() >= 256 || !step["enabled"].isBool())
					{
						return invalid("Invalid step index or enabled flag.");
					}
				}
			}
			if (cmd.kind == "set_automation_points")
			{
				auto* automation = dynamic_cast<AutomationClip*>(genericClip);
				auto* model = AiParameterRegistry::instance()->model(cmd.parameterId);
				const auto points = cmd.rawParams["points"].toArray();
				ParameterInfo parameter;
				AiParameterRegistry::instance()->getParameterInfo(cmd.parameterId, parameter);
				if (deletedTracks.contains(registry->lookupTrack(parameter.ownerId)))
				{
					return invalid("Automation parameter owner deleted earlier in plan.");
				}
				const QString interpolation = cmd.rawParams["interpolation"].toString("linear");
				if ((!automation && !newAutomation) || !model || model->controllerConnection() || points.isEmpty()
					|| points.size() > 512 || (interpolation != "linear" && interpolation != "discrete"))
				{
					return invalid("Invalid automation target, points or interpolation.");
				}
				for (const auto& value : points)
				{
					auto point = value.toObject();
					double tick = point["positionTicks"].toDouble(-1), amount = point["value"].toDouble();
					if (!point["positionTicks"].isDouble() || tick < 0
						|| tick + (genericClip ? genericClip->startPosition().getTicks() : cmd.positionTicks)
							> std::numeric_limits<tick_t>::max()
						|| tick != std::floor(tick) || !point["value"].isDouble() || !std::isfinite(amount)
						|| amount < model->minValue<double>() || amount > model->maxValue<double>())
					{
						return invalid("Invalid automation point.");
					}
				}
			}
		}
		else { return {ErrorCode::UnsupportedOperation, "Command not implemented: " + cmd.kind}; }
	}
	// Validate the complete graph, including channels/routes created within this batch.
	QSet<int> visiting, done;
	std::function<bool(int)> cycle = [&](int channel) {
		if (visiting.contains(channel)) { return true; }
		if (done.contains(channel)) { return false; }
		visiting.insert(channel);
		for (int next : routes.value(channel))
		{
			if (cycle(next)) { return true; }
		}
		visiting.remove(channel);
		done.insert(channel);
		return false;
	};
	for (int i = 0; i < channels; ++i)
	{
		if (cycle(i)) { return invalid("Mixer routing contains a cycle."); }
	}
	return {};
}

ApplyResult AiEditTransaction::executePlan(Song* song, std::shared_ptr<AiChangePlan> plan, const QString& operationKey,
	qint64 deadlineEpochMs, const std::function<void(int)>& afterCommand)
{
	Q_UNUSED(operationKey);
	ApplyResult result;
	if (!song || !plan)
	{
		result.error = {ErrorCode::NotReady, "Song or plan unavailable."};
		return result;
	}
	if (plan->projectId() != AiProjectRevision::instance()->projectId())
	{
		result.error = {ErrorCode::ProjectChanged, "Project changed."};
		return result;
	}
	if (plan->isExpired())
	{
		result.error = {ErrorCode::PlanExpired, "Plan expired."};
		return result;
	}
	if (plan->expectedRevision() != AiProjectRevision::instance()->currentRevision())
	{
		result.error = {ErrorCode::RevisionConflict, "Revision changed."};
		return result;
	}
	if (!song->isStopped() || song->isLoadingProject() || song->isExporting())
	{
		result.error = {ErrorCode::Busy, "Project is playing, loading or exporting."};
		return result;
	}
	result.error = validateCommands(plan->commands());
	if (result.error.hasError()) { return result; }
	auto* journal = Engine::projectJournal();
	if (journal->isGroupOpen() || !journal->isJournalling())
	{
		result.error = {ErrorCode::Busy, "Journal unavailable."};
		return result;
	}
	const bool wasModified = song->isModified();
	RevisionGuard revision;
	result.undoGroupId = "undo_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
	ProjectJournal::GroupGuard group(journal, plan->summary(), result.undoGroupId);
	QSet<Track*> owners;
	QSet<TrackContainer*> containers;
	QHash<JournallingObject*, QByteArray> beforeStates;
	qint64 checkpointBytes = 0;
	const bool capturesMixer
		= std::any_of(plan->commands().begin(), plan->commands().end(), [](const ChangeCommand& command) {
			  return command.kind == "create_mixer_channel" || command.kind == "set_mixer_send";
		  });
	QHash<QString, Note*> resolvedNotes;
	auto* registry = AiObjectRegistry::instance();
	// Capture owners only; clip children are restored by their owning track.
	for (const auto& cmd : plan->commands())
	{
		if (!cmd.trackId.isEmpty()) { owners.insert(registry->lookupTrack(cmd.trackId)); }
		if (cmd.kind == "set_automation_points" && cmd.targetClipId.isEmpty()) { containers.insert(song); }
		if (cmd.kind == "delete_track") { containers.insert(registry->lookupTrack(cmd.trackId)->trackContainer()); }
		if (auto* generic = registry->lookupClip(cmd.targetClipId)) { owners.insert(generic->getTrack()); }
		if (auto* clip = registry->lookupMidiClip(cmd.targetClipId))
		{
			for (const auto& n : cmd.notes)
			{
				if (!n.noteRef.isEmpty()) { resolvedNotes[n.noteRef] = resolveNote(n.noteRef, clip); }
			}
			for (const auto& ref : cmd.noteRefs)
			{
				resolvedNotes[ref] = resolveNote(ref, clip);
			}
		}
	}
	auto capture = [&](JournallingObject* object) {
		if (!object || beforeStates.contains(object)) { return; }
		DataFile state(DataFile::Type::JournalData);
		ProjectJournal::saveJournalScope(object, state, true);
		if (state.content().firstChildElement().isNull())
		{
			throw DomainError{ErrorCode::Busy, "Object cannot be journalled."};
		}
		auto bytes = state.toByteArray();
		checkpointBytes += bytes.size();
		if (checkpointBytes > 64 * 1024 * 1024)
		{
			throw DomainError{ErrorCode::Busy, "Transaction restore data exceeds 64 MiB."};
		}
		beforeStates.insert(object, bytes);
		journal->addJournalCheckPoint(object);
	};
	try
	{
		auto covered = [&](Track* owner) {
			return owner
				&& (containers.contains(owner->trackContainer())
					|| (containers.contains(song) && owner->trackContainer() == Engine::patternStore()));
		};
		if (containers.contains(song)) { capture(song); }
		if (containers.contains(song) || containers.contains(Engine::patternStore()))
		{
			capture(Engine::patternStore());
		}
		for (auto* owner : owners)
		{
			if (!covered(owner)) { capture(owner); }
		}
		// Built-in parameters currently belong to the song; capture once before disabling inner checkpoints.
		capture(&song->tempoModel());
		for (const auto& cmd : plan->commands())
			if (cmd.kind == "set_parameter")
			{
				ParameterInfo info;
				AiParameterRegistry::instance()->getParameterInfo(cmd.parameterId, info);
				auto* owner = registry->lookupTrack(info.ownerId);
				if ((!capturesMixer || !info.ownerId.startsWith("mix_"))
					&& (!owner || (!owners.contains(owner) && !covered(owner))))
				{
					capture(AiParameterRegistry::instance()->model(cmd.parameterId));
				}
			}
		for (const auto& cmd : plan->commands())
			if (cmd.kind == "create_mixer_channel" || cmd.kind == "set_mixer_send")
			{
				capture(Engine::mixer());
				break;
			}
	}
	catch (const DomainError& error)
	{
		journal->discardJournalGroup();
		result.error = error;
		return result;
	}
	if (deadlineEpochMs > 0 && QDateTime::currentMSecsSinceEpoch() > deadlineEpochMs)
	{
		journal->discardJournalGroup();
		result.error = {ErrorCode::Timeout, "Deadline expired during transaction preparation."};
		return result;
	}
	journal->setJournalling(false);
	QHash<QString, MidiClip*> created;
	bool changed = false;
	int completedCommands = 0;
	try
	{
		for (const auto& cmd : plan->commands())
		{
			auto* track = registry->lookupTrack(cmd.trackId);
			auto* clip = registry->lookupMidiClip(cmd.targetClipId);
			auto* generic = registry->lookupClip(cmd.targetClipId);
			if (cmd.kind == "set_tempo")
			{
				changed |= song->getTempo() != cmd.bpm;
				song->setTempo(cmd.bpm);
			}
			else if (cmd.kind == "rename_track")
			{
				changed |= track->name() != cmd.newName;
				track->setName(cmd.newName);
			}
			else if (cmd.kind == "set_track_mute")
			{
				changed |= track->isMuted() != cmd.muted;
				track->setMuted(cmd.muted);
			}
			else if (cmd.kind == "set_parameter")
			{
				ParameterInfo info;
				AiParameterRegistry::instance()->getParameterInfo(cmd.parameterId, info);
				changed |= info.value != cmd.paramValue;
				auto error = AiParameterRegistry::instance()->setParameterValue(cmd.parameterId, cmd.paramValue);
				if (error.hasError()) { throw error; }
			}
			else if (cmd.kind == "create_midi_clip")
			{
				auto* createdClip = dynamic_cast<MidiClip*>(track->createClip(TimePos(cmd.positionTicks)));
				if (!createdClip) { throw DomainError{ErrorCode::InternalError, "Failed to create MIDI clip."}; }
				createdClip->changeLength(TimePos(cmd.lengthTicks));
				createdClip->setAutoResize(false);
				created[cmd.clientRef] = createdClip;
				result.createdObjects[cmd.clientRef] = registry->registerClip(createdClip);
				changed = true;
			}
			else if (cmd.kind == "add_notes")
			{
				if (!cmd.targetClipClientRef.isEmpty()) { clip = created.value(cmd.targetClipClientRef); }
				for (const auto& n : cmd.notes)
				{
					Note note(TimePos(n.lengthTicks), TimePos(n.positionTicks), n.midiKey, n.volumePercent, n.pan);
					note.setType(n.noteType == "step" ? Note::Type::Step : Note::Type::Regular);
					clip->addNote(note, false);
					++result.addedNotes;
				}
				changed = true;
			}
			else if (cmd.kind == "update_notes")
			{
				int noteIndex = 0;
				for (const auto& n : cmd.notes)
				{
					auto* note = resolvedNotes.value(n.noteRef);
					const auto supplied = cmd.rawParams["notes"].toArray();
					const auto fields = noteIndex < supplied.size() ? supplied.at(noteIndex).toObject() : QJsonObject();
					++noteIndex;
					note->setKey(n.midiKey);
					note->setPos(TimePos(n.positionTicks));
					note->setLength(TimePos(n.lengthTicks));
					if (cmd.rawParams.isEmpty() || fields.contains("volumePercent"))
					{
						note->setVolume(n.volumePercent);
					}
					if (cmd.rawParams.isEmpty() || fields.contains("pan")) { note->setPanning(n.pan); }
					if (cmd.rawParams.isEmpty() || fields.contains("noteType"))
					{
						note->setType(n.noteType == "step" ? Note::Type::Step : Note::Type::Regular);
					}
				}
				clip->rearrangeAllNotes();
				clip->updateLength();
				emit clip->dataChanged();
				changed = true;
			}
			else if (cmd.kind == "remove_notes")
			{
				for (const auto& ref : cmd.noteRefs)
				{
					clip->removeNote(resolvedNotes.value(ref));
				}
				changed = true;
			}
			else if (cmd.kind == "transpose_notes")
			{
				for (auto* n : clip->notes())
				{
					if (cmd.noteRefs.isEmpty()
						|| std::any_of(cmd.noteRefs.begin(), cmd.noteRefs.end(),
							[&](const QString& ref) { return resolvedNotes.value(ref) == n; }))
					{
						n->setKey(n->key() + cmd.transposeSemitones);
					}
				}
				clip->rearrangeAllNotes();
				emit clip->dataChanged();
				changed |= !clip->notes().empty();
			}
			else if (cmd.kind == "create_mixer_channel")
			{
				const int index = Engine::mixer()->createChannel();
				if (!cmd.clientRef.isEmpty())
				{
					result.createdObjects[cmd.clientRef] = registry->registerMixerChannel(index);
				}
				changed = true;
			}
			else if (cmd.kind == "set_mixer_send")
			{
				const int from = cmd.rawParams.contains("fromChannelId")
					? registry->lookupMixerChannel(cmd.rawParams["fromChannelId"].toString())
					: cmd.mixerChannelIndex;
				const int to = cmd.rawParams.contains("toChannelId")
					? registry->lookupMixerChannel(cmd.rawParams["toChannelId"].toString())
					: cmd.mixerSendTargetIndex;
				auto* model = Engine::mixer()->channelSendModel(from, to);
				changed |= !model || model->value() != cmd.mixerSendAmount;
				Engine::mixer()->createChannelSend(from, to, cmd.mixerSendAmount);
			}
			else if (cmd.kind == "move_clip")
			{
				generic->movePosition(TimePos(cmd.positionTicks));
				changed = true;
			}
			else if (cmd.kind == "resize_clip")
			{
				generic->setAutoResize(false);
				generic->changeLength(TimePos(cmd.lengthTicks));
				changed = true;
			}
			else if (cmd.kind == "duplicate_clip")
			{
				auto* copy = generic->clone();
				copy->movePosition(TimePos(cmd.positionTicks));
				result.createdObjects[cmd.clientRef] = registry->registerClip(copy);
				if (auto* midi = dynamic_cast<MidiClip*>(copy)) { created[cmd.clientRef] = midi; }
				changed = true;
			}
			else if (cmd.kind == "delete_clip")
			{
				delete generic;
				changed = true;
			}
			else if (cmd.kind == "delete_track")
			{
				delete track;
				changed = true;
			}
			else if (cmd.kind == "assign_mixer_channel")
			{
				auto* instrument = dynamic_cast<InstrumentTrack*>(track);
				instrument->mixerChannelModel()->setValue(
					registry->lookupMixerChannel(cmd.rawParams["channelId"].toString()));
				changed = true;
			}
			else if (cmd.kind == "assign_pattern")
			{
				auto* arrangement = track->createClip(TimePos(cmd.positionTicks));
				arrangement->changeLength(TimePos(cmd.lengthTicks));
				if (!cmd.clientRef.isEmpty())
				{
					result.createdObjects[cmd.clientRef] = registry->registerClip(arrangement);
				}
				changed = true;
			}
			else if (cmd.kind == "set_pattern_steps")
			{
				for (const auto& value : cmd.rawParams["steps"].toArray())
				{
					auto step = value.toObject();
					clip->setStep(step["stepIndex"].toInt(), step["enabled"].toBool());
				}
				changed = true;
			}
			else if (cmd.kind == "set_automation_points")
			{
				auto* automation = dynamic_cast<AutomationClip*>(generic);
				if (!automation)
				{
					auto* automationTrack = Track::create(Track::Type::Automation, song);
					automation = dynamic_cast<AutomationClip*>(automationTrack->createClip(TimePos(cmd.positionTicks)));
					automation->changeLength(TimePos(cmd.lengthTicks));
					automation->setAutoResize(false);
					result.createdObjects[cmd.clientRef] = registry->registerClip(automation);
					result.createdObjects[cmd.clientRef + ".track"] = registry->registerTrack(automationTrack);
				}
				automation->clear();
				automation->clearObjects();
				automation->addObject(AiParameterRegistry::instance()->model(cmd.parameterId));
				automation->setProgressionType(cmd.rawParams["interpolation"].toString("linear") == "linear"
						? AutomationClip::ProgressionType::Linear
						: AutomationClip::ProgressionType::Discrete);
				for (const auto& value : cmd.rawParams["points"].toArray())
				{
					auto point = value.toObject();
					automation->putValue(TimePos(point["positionTicks"].toInt()), point["value"].toDouble(), false);
				}
				changed = true;
			}
			if (afterCommand) { afterCommand(++completedCommands); }
		}
		changed = false;
		for (auto it = beforeStates.cbegin(); it != beforeStates.cend(); ++it)
		{
			DataFile state(DataFile::Type::JournalData);
			ProjectJournal::saveJournalScope(it.key(), state, true);
			changed |= state.toByteArray() != it.value();
		}
		journal->setJournalling(true);
		if (changed)
		{
			group.commit();
			song->setModified();
			revision.changed = true;
		}
		else
		{
			journal->discardJournalGroup();
			result.undoGroupId.clear();
		}
	}
	catch (const DomainError& error)
	{
		journal->setJournalling(true);
		journal->cancelJournalGroup();
		song->setModified(wasModified);
		registry->invalidateAll();
		result.error = error;
		return result;
	}
	catch (...)
	{
		journal->setJournalling(true);
		journal->cancelJournalGroup();
		song->setModified(wasModified);
		registry->invalidateAll();
		result.error = {ErrorCode::InternalError, "Transaction rolled back after an exception."};
		return result;
	}
	result.ok = true;
	result.status = changed ? "committed" : "unchanged";
	result.newRevision = AiProjectRevision::instance()->currentRevision() + (changed ? 1 : 0);
	return result;
}

} // namespace lmms::ai
