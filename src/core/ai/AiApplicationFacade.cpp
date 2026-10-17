#include "AiApplicationFacade.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

#include "AiChangePlan.h"
#include "AiControlService.h"
#include "AiEditTransaction.h"
#include "AiFilePolicy.h"
#include "AiFacadeEdit.h"
#include "AiJobManager.h"
#include "AiObjectRegistry.h"
#include "AiParameterRegistry.h"
#include "AiProjectRevision.h"
#include "AutomationClip.h"
#include "DataFile.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "SampleBuffer.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms::ai {

namespace {

QJsonObject failure(ErrorCode code, const QString& message)
{
	return {{"ok", false}, {"error", DomainError{code, message}.toJson()}};
}
QString pageCursor(int index, const QString& scope)
{
	QJsonObject value{{"index", index}, {"scope", scope}, {"project", AiProjectRevision::instance()->projectId()},
		{"revision", static_cast<qint64>(AiProjectRevision::instance()->currentRevision())}};
	return QString::fromLatin1(QJsonDocument(value)
			.toJson(QJsonDocument::Compact)
			.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
int cursorIndex(const QString& cursor, const QString& scope)
{
	if (cursor.isEmpty()) { return 0; }
	if (cursor.size() > 4096) { return -1; }
	const auto decoded = QByteArray::fromBase64(
		cursor.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
	if (decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != cursor.toLatin1())
	{
		return -1;
	}
	const auto value = QJsonDocument::fromJson(decoded).object();
	if (value["scope"].toString() != scope || value["project"].toString() != AiProjectRevision::instance()->projectId()
		|| value["revision"].toVariant().toULongLong() != AiProjectRevision::instance()->currentRevision()
		|| !value["index"].isDouble() || value["index"].toInt(-1) < 0)
	{
		return -1;
	}
	return value["index"].toInt();
}
} // namespace

AiApplicationFacade* AiApplicationFacade::instance()
{
	static AiApplicationFacade s_instance;
	return &s_instance;
}

QJsonObject AiApplicationFacade::getStatus() const
{
	QJsonObject res;
	res["ok"] = true;
	res["instanceId"] = AiControlService::instance()->instanceId();
	res["ready"] = AiControlService::instance()->isReady();
	res["version"] = "1.3.0-alpha";
	res["lastError"] = AiControlService::instance()->lastError();
	res["diagnostics"] = AiControlService::instance()->diagnostics();
	res["permissions"]
		= QJsonArray::fromStringList(permissionsToStringList(AiControlService::instance()->grantedPermissions()));
	res["busy"] = !AiControlService::instance()->isReady();
	res["state"] = !AiControlService::instance()->isRunning()		 ? "stopped"
		: Engine::getSong() && Engine::getSong()->isLoadingProject() ? "loading"
		: AiControlService::instance()->isReady()					 ? "ready"
																	 : "starting";

	QJsonObject caps;
	caps["editMidi"] = true;
	caps["protocolVersions"] = QJsonArray{"2026-07-28", "2025-11-25"};
	caps["maxBatchCommands"] = static_cast<int>(MAX_BATCH_COMMANDS);
	caps["maxBatchNotes"] = static_cast<int>(MAX_BATCH_NOTES);
	caps["operationRetentionSeconds"] = 24 * 3600;
	caps["createInternalInstrument"] = true;
	caps["renderSnapshot"] = true;
	caps["mixerRouting"] = true;
	res["capabilities"] = caps;

	return res;
}

QJsonObject AiApplicationFacade::getProject(const QString& optProjectId) const
{
	Song* song = Engine::getSong();
	if (!song)
	{
		QJsonObject res;
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Song is not available."}.toJson();
		return res;
	}

	QString projId = AiProjectRevision::instance()->projectId();
	if (!optProjectId.isEmpty() && optProjectId != projId)
	{
		QJsonObject res;
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::ProjectChanged, "Project ID does not match currently active document."}.toJson();
		return res;
	}

	ProjectSummary summary;
	summary.projectId = projId;
	summary.projectRevision = AiProjectRevision::instance()->currentRevision();
	summary.name = song->projectFileName().isEmpty() ? "Untitled" : song->projectFileName();
	summary.modified = song->isModified();
	summary.tempoBpm = song->getTempo();
	summary.timeSigNumerator = song->getTimeSigModel().getNumerator();
	summary.timeSigDenominator = song->getTimeSigModel().getDenominator();
	summary.ticksPerQuarter = 48;
	summary.ticksPerBar = TimePos::ticksPerBar();
	summary.lengthTicks = song->length() * summary.ticksPerBar;
	summary.playMode = (song->playMode() == Song::PlayMode::Pattern) ? "pattern" : "song";
	summary.isPlaying = song->isPlaying();
	summary.isPaused = song->isPaused();

	QJsonObject res;
	res["ok"] = true;
	res["instanceId"] = AiControlService::instance()->instanceId();
	res["projectId"] = summary.projectId;
	res["projectRevision"] = static_cast<qint64>(summary.projectRevision);
	res["data"] = summary.toJson();
	return res;
}

QJsonObject AiApplicationFacade::listTracks(const QString& container, const QString& cursor, int limit) const
{
	QJsonObject res;
	if (!container.isEmpty() && container != "song" && container != "pattern_store")
	{
		return failure(ErrorCode::ValidationError, "Unknown container.");
	}
	TrackContainer* tc = (container == "pattern_store") ? static_cast<TrackContainer*>(Engine::patternStore())
														: static_cast<TrackContainer*>(Engine::getSong());
	if (!tc)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Requested container is not available."}.toJson();
		return res;
	}

	const QString cursorScope = "tracks:" + container;
	const int parsedCursor = cursorIndex(cursor, cursorScope);
	if (parsedCursor < 0) { return failure(ErrorCode::StaleCursor, "Invalid or stale cursor."); }
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	if (limit > static_cast<int>(MAX_PAGE_LIMIT)) { limit = MAX_PAGE_LIMIT; }

	const auto& tracks = tc->tracks();
	QJsonArray items;
	int count = 0;
	int nextIdx = parsedCursor;

	for (size_t i = parsedCursor; i < tracks.size() && count < limit; ++i)
	{
		Track* t = tracks[i];
		TrackSummary ts;
		ts.trackId = AiObjectRegistry::instance()->registerTrack(t);
		ts.name = t->name();
		ts.type = QString::number(static_cast<int>(t->type()));
		ts.container = (container == "pattern_store") ? "pattern_store" : "song";
		ts.muted = t->isMuted();
		ts.solo = t->isSolo();
		ts.clipCount = t->numOfClips();

		if (auto it = dynamic_cast<InstrumentTrack*>(t))
		{
			ts.type = "instrument";
			ts.instrumentKey = it->instrument() ? QString::fromUtf8(it->instrument()->descriptor()->name) : QString();
			ts.volume = it->volumeModel()->value();
			ts.panning = it->panningModel()->value();
			ts.mixerChannel = it->mixerChannelModel()->value();
		}

		auto item = ts.toJson();
		if (auto* pattern = dynamic_cast<PatternTrack*>(t))
		{
			item["type"] = "pattern";
			item["patternId"] = ts.trackId;
			item["patternIndex"] = pattern->patternIndex();
		}
		items.append(item);
		count++;
		nextIdx = static_cast<int>(i + 1);
	}

	res["ok"] = true;
	res["items"] = items;
	if (nextIdx < static_cast<int>(tracks.size())) { res["nextCursor"] = pageCursor(nextIdx, cursorScope); }
	return res;
}

QJsonObject AiApplicationFacade::listClips(
	const QString& trackId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const
{
	QJsonObject res;
	if (startTick < 0 || endTick < 0 || (endTick && endTick < startTick))
	{
		return failure(ErrorCode::ValidationError, "Invalid tick range.");
	}
	Track* t = AiObjectRegistry::instance()->lookupTrack(trackId);
	if (!t)
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::StaleHandle, QString("Track not found for handle: %1").arg(trackId)}.toJson();
		return res;
	}

	const QString cursorScope = "clips:" + trackId + ":" + QString::number(startTick) + ":" + QString::number(endTick);
	const int parsedCursor = cursorIndex(cursor, cursorScope);
	if (parsedCursor < 0) { return failure(ErrorCode::StaleCursor, "Invalid or stale cursor."); }
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	if (limit > static_cast<int>(MAX_PAGE_LIMIT)) { limit = MAX_PAGE_LIMIT; }

	const auto& clips = t->getClips();
	QJsonArray items;
	int count = 0;
	int nextIdx = parsedCursor;

	for (int i = parsedCursor; i < clips.size() && count < limit; ++i)
	{
		nextIdx = i + 1;
		Clip* c = clips[i];
		if (endTick > 0 && (c->startPosition().getTicks() > endTick || c->endPosition().getTicks() < startTick))
		{
			continue;
		}

		ClipSummary cs;
		cs.clipId = AiObjectRegistry::instance()->registerClip(c);
		cs.trackId = trackId;
		cs.name = c->name();
		cs.positionTicks = c->startPosition().getTicks();
		cs.lengthTicks = c->length().getTicks();
		cs.muted = c->isMuted();

		if (auto mc = dynamic_cast<MidiClip*>(c))
		{
			cs.type = "midi";
			cs.noteCount = mc->notes().size();
		}
		else { cs.type = "other"; }

		items.append(cs.toJson());
		count++;
		nextIdx = i + 1;
	}

	res["ok"] = true;
	res["items"] = items;
	if (nextIdx < clips.size()) { res["nextCursor"] = pageCursor(nextIdx, cursorScope); }
	return res;
}

QJsonObject AiApplicationFacade::getClipSummary(const QString& clipId) const
{
	auto* clip = AiObjectRegistry::instance()->lookupClip(clipId);
	if (!clip) { return failure(ErrorCode::StaleHandle, "Unknown clip."); }
	ClipSummary summary;
	summary.clipId = clipId;
	summary.trackId = AiObjectRegistry::instance()->registerTrack(clip->getTrack());
	summary.name = clip->name();
	summary.positionTicks = clip->startPosition().getTicks();
	summary.lengthTicks = clip->length().getTicks();
	summary.muted = clip->isMuted();
	summary.type = dynamic_cast<AutomationClip*>(clip) ? "automation" : "other";
	if (auto* midi = dynamic_cast<MidiClip*>(clip))
	{
		summary.type = "midi";
		summary.noteCount = midi->notes().size();
	}
	return {{"ok", true}, {"projectId", AiProjectRevision::instance()->projectId()},
		{"projectRevision", static_cast<qint64>(AiProjectRevision::instance()->currentRevision())},
		{"data", summary.toJson()}};
}

QJsonObject AiApplicationFacade::getAutomation(const QString& clipId, const QString& cursor, int limit) const
{
	auto* clip = dynamic_cast<AutomationClip*>(AiObjectRegistry::instance()->lookupClip(clipId));
	if (!clip) { return failure(ErrorCode::StaleHandle, "Unknown automation clip."); }
	const QString scope = "automation:" + clipId;
	int start = cursorIndex(cursor, scope);
	if (start < 0) { return failure(ErrorCode::StaleCursor, "Stale automation cursor."); }
	limit = qBound(1, limit, static_cast<int>(MAX_PAGE_LIMIT));
	QJsonArray points;
	int index = 0;
	auto it = clip->getTimeMap().cbegin();
	for (; it != clip->getTimeMap().cend() && index < start; ++it, ++index) {}
	for (; it != clip->getTimeMap().cend() && points.size() < limit; ++it, ++index)
	{
		points.append(QJsonObject{
			{"positionTicks", it.key()}, {"value", it.value().getInValue()}, {"outValue", it.value().getOutValue()}});
	}
	QJsonArray parameters;
	AiParameterRegistry::instance()->registerBuiltins(Engine::getSong());
	for (const auto& parameter : AiParameterRegistry::instance()->listParameters({}, {}, 1000000))
		for (const auto& model : clip->objects())
			if (model && model == AiParameterRegistry::instance()->model(parameter.parameterId))
			{
				parameters.append(parameter.parameterId);
			}
	QJsonObject result{{"ok", true}, {"clipId", clipId}, {"points", points}, {"parameterIds", parameters},
		{"interpolation",
			clip->progressionType() == AutomationClip::ProgressionType::Discrete	 ? "discrete"
				: clip->progressionType() == AutomationClip::ProgressionType::Linear ? "linear"
																					 : "cubic"}};
	if (it != clip->getTimeMap().cend()) { result["nextCursor"] = pageCursor(index, scope); }
	return result;
}

QJsonObject AiApplicationFacade::getNotes(
	const QString& clipId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const
{
	QJsonObject res;
	if (startTick < 0 || endTick < 0 || (endTick && endTick < startTick))
	{
		return failure(ErrorCode::ValidationError, "Invalid tick range.");
	}
	MidiClip* mc = AiObjectRegistry::instance()->lookupMidiClip(clipId);
	if (!mc)
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::StaleHandle, QString("MidiClip not found for handle: %1").arg(clipId)}.toJson();
		return res;
	}

	uint64_t clipRev = AiProjectRevision::instance()->currentClipRevision(clipId);

	const QString cursorScope = "notes:" + clipId + ":" + QString::number(startTick) + ":" + QString::number(endTick);
	const int parsedCursor = cursorIndex(cursor, cursorScope);
	if (parsedCursor < 0) { return failure(ErrorCode::StaleCursor, "Invalid or stale cursor."); }
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	if (limit > static_cast<int>(MAX_PAGE_LIMIT)) { limit = MAX_PAGE_LIMIT; }

	const auto& notes = mc->notes();
	QJsonArray items;
	int count = 0;
	int nextIdx = parsedCursor;

	for (int i = parsedCursor; i < notes.size() && count < limit; ++i)
	{
		nextIdx = i + 1;
		const Note* n = notes[i];
		if (endTick > 0
			&& (n->pos().getTicks() > endTick || (n->pos().getTicks() + n->length().getTicks()) < startTick))
		{
			continue;
		}

		NoteData nd;
		nd.noteRef = AiObjectRegistry::instance()->generateNoteRef(clipId, clipRev, i);
		nd.positionTicks = n->pos().getTicks();
		nd.lengthTicks = n->length().getTicks();
		nd.midiKey = n->key();
		nd.volumePercent = n->getVolume();
		nd.pan = n->getPanning();
		nd.hasDetuning = n->hasDetuningInfo();
		nd.noteType = (n->type() == Note::Type::Step) ? "step" : "regular";

		items.append(nd.toJson());
		count++;
		nextIdx = i + 1;
	}

	res["ok"] = true;
	res["clipId"] = clipId;
	res["clipRevision"] = static_cast<qint64>(clipRev);
	res["notes"] = items;
	if (nextIdx < notes.size()) { res["nextCursor"] = pageCursor(nextIdx, cursorScope); }
	return res;
}

QJsonObject AiApplicationFacade::listParameters(const QString& ownerId, const QString& cursor, int limit) const
{
	AiParameterRegistry::instance()->registerBuiltins(Engine::getSong());
	const QString scope = "parameters:" + ownerId;
	const int start = cursorIndex(cursor, scope);
	if (start < 0) { return failure(ErrorCode::StaleCursor, "Stale parameter cursor."); }
	limit = qBound(1, limit, static_cast<int>(MAX_PAGE_LIMIT));
	const auto parameters = AiParameterRegistry::instance()->listParameters(ownerId, {}, 1000000);
	QJsonArray items;
	int index = start;
	for (; index < parameters.size() && items.size() < limit; ++index)
	{
		items.append(parameters[index].toJson());
	}
	QJsonObject result{{"ok", true}, {"parameters", items}};
	if (index < parameters.size()) { result["nextCursor"] = pageCursor(index, scope); }
	return result;
}

QJsonObject AiApplicationFacade::listInstruments(const QString& typeFilter, const QString& cursor, int limit) const
{
	if (!typeFilter.isEmpty() && typeFilter != "internal")
	{
		return failure(ErrorCode::ValidationError, "Only internal instruments are supported.");
	}
	const QString scope = "instruments:" + typeFilter;
	const int start = cursorIndex(cursor, scope);
	if (start < 0) { return failure(ErrorCode::StaleCursor, "Stale instrument cursor."); }
	auto descriptors = getPluginFactory()->descriptors(Plugin::Type::Instrument);
	std::sort(
		descriptors.begin(), descriptors.end(), [](auto* a, auto* b) { return QString(a->name) < QString(b->name); });
	QJsonArray items;
	int index = start;
	limit = qBound(1, limit, static_cast<int>(MAX_PAGE_LIMIT));
	const QStringList supported{
		"tripleoscillator", "bitinvader", "organic", "kicker", "audiofileprocessor", "monstro", "watsyn", "vibed"};
	for (; index < descriptors.size() && items.size() < limit; ++index)
	{
		const auto* descriptor = descriptors[index];
		if (!supported.contains(descriptor->name)) { continue; }
		items.append(QJsonObject{
			{"key", descriptor->name}, {"name", descriptor->displayName}, {"isInternal", true}, {"status", "allowed"}});
	}
	QJsonObject result{{"ok", true}, {"instruments", items}};
	if (index < descriptors.size()) { result["nextCursor"] = pageCursor(index, scope); }
	return result;
}

QJsonObject AiApplicationFacade::getMixer(int channelIndex) const
{
	QJsonObject res;
	Mixer* mixer = Engine::mixer();
	if (!mixer)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Mixer not available."}.toJson();
		return res;
	}

	QJsonArray channelsArr;
	int start = (channelIndex >= 0) ? channelIndex : 0;
	int end = (channelIndex >= 0) ? channelIndex + 1 : mixer->numChannels();

	for (int i = start; i < end && i < mixer->numChannels(); ++i)
	{
		MixerChannel* ch = mixer->mixerChannel(i);
		if (!ch) { continue; }

		MixerChannelSummary mcs;
		mcs.channelIndex = i;
		// Preserve the legacy display index and expose a generation-scoped identity.
		const QString channelId = AiObjectRegistry::instance()->registerMixerChannel(i);
		mcs.name = ch->m_name.isEmpty() ? (i == 0 ? "Master" : QString("FX %1").arg(i)) : ch->m_name;
		mcs.muted = ch->m_muteModel.value();
		mcs.solo = ch->m_soloModel.value();
		mcs.volume = ch->m_volumeModel.value();
		mcs.isMaster = (i == 0);

		for (const auto& route : ch->m_sends)
		{
			if (route)
			{
				MixerSendInfo si;
				si.targetChannelIndex = route->receiverIndex();
				si.amount = route->amount()->value();
				mcs.sends.push_back(si);
			}
		}

		auto channel = mcs.toJson();
		channel["channelId"] = channelId;
		channelsArr.append(channel);
	}

	res["ok"] = true;
	res["channels"] = channelsArr;
	return res;
}

QJsonObject AiApplicationFacade::setTransport(const QString& action, tick_t seekTicks)
{
	QJsonObject res;
	Song* song = Engine::getSong();
	if (!song)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Song is not available."}.toJson();
		return res;
	}

	if (action == "play")
	{
		if (song->isPaused() && song->playMode() == Song::PlayMode::Song) { song->togglePause(); }
		else if (!song->isPlaying() || song->playMode() != Song::PlayMode::Song) { song->playSong(); }
	}
	else if (action == "pause")
	{
		if (song->isPlaying() && !song->isPaused()) { song->togglePause(); }
	}
	else if (action == "stop") { song->stop(); }
	else if (action == "seek") { song->setPlayPos(seekTicks); }
	else
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::ValidationError, QString("Unknown transport action: %1").arg(action)}.toJson();
		return res;
	}

	res["ok"] = true;
	res["action"] = action;
	res["isPlaying"] = song->isPlaying();
	res["isPaused"] = song->isPaused();
	res["positionTicks"] = static_cast<qint64>(song->getPlayPos().getTicks());
	return res;
}

QJsonObject AiApplicationFacade::previewChanges(
	const QString& projectId, uint64_t expectedRevision, const QJsonArray& changes)
{
	QJsonObject res;
	Song* song = Engine::getSong();
	if (!song)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Song not available."}.toJson();
		return res;
	}

	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (currentRev != expectedRevision)
	{
		res["ok"] = false;
		DomainError err;
		err.code = ErrorCode::RevisionConflict;
		err.message = QString("Project revision conflict: expected %1 but current is %2.")
						  .arg(expectedRevision)
						  .arg(currentRev);
		QJsonObject details;
		details["expectedRevision"] = static_cast<qint64>(expectedRevision);
		details["actualRevision"] = static_cast<qint64>(currentRev);
		err.details = details;
		res["error"] = err.toJson();
		return res;
	}

	if (projectId != AiProjectRevision::instance()->projectId())
	{
		return {{"ok", false}, {"error", DomainError{ErrorCode::ProjectChanged, "Project changed."}.toJson()}};
	}
	if (changes.isEmpty() || changes.size() > static_cast<int>(MAX_BATCH_COMMANDS))
	{
		return {{"ok", false}, {"error", DomainError{ErrorCode::ValidationError, "Expected 1..64 commands."}.toJson()}};
	}
	AiParameterRegistry::instance()->registerBuiltins(song);
	std::vector<ChangeCommand> cmds;
	for (const auto& v : changes)
	{
		DomainError cmdErr;
		ChangeCommand cmd = ChangeCommand::fromJson(v.toObject(), cmdErr);
		if (cmdErr.hasError())
		{
			res["ok"] = false;
			res["error"] = cmdErr.toJson();
			return res;
		}
		cmds.push_back(cmd);
	}

	auto validation = AiEditTransaction::validateCommands(cmds);
	if (validation.hasError()) { return {{"ok", false}, {"error", validation.toJson()}}; }
	QString planId = QString("plan_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	auto plan = std::make_shared<AiChangePlan>(planId, projectId, expectedRevision, cmds);
	if (AiChangePlanManager::instance()->storePlan(plan).isEmpty())
	{
		return failure(ErrorCode::Busy, "Plan register full.");
	}

	res["ok"] = true;
	res["plan"] = plan->toJson();
	return res;
}

QJsonObject AiApplicationFacade::applyChanges(const QString& projectId, uint64_t expectedRevision,
	const QString& planId, const QString& operationKey, qint64 deadlineEpochMs)
{
	QJsonObject res;
	auto plan = AiChangePlanManager::instance()->lookupPlan(planId);
	if (!plan)
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::PlanExpired, QString("Plan '%1' not found or expired.").arg(planId)}.toJson();
		return res;
	}

	if (projectId != plan->projectId() || projectId != AiProjectRevision::instance()->projectId()
		|| expectedRevision != plan->expectedRevision())
	{
		return {{"ok", false},
			{"error", DomainError{ErrorCode::ProjectChanged, "Plan project or revision does not match."}.toJson()}};
	}
	ApplyResult applyRes = AiEditTransaction::executePlan(Engine::getSong(), plan, operationKey, deadlineEpochMs);
	if (!applyRes.ok)
	{
		res["ok"] = false;
		res["error"] = applyRes.error.toJson();
		return res;
	}

	AiChangePlanManager::instance()->removePlan(planId);

	res["ok"] = true;
	res["instanceId"] = AiControlService::instance()->instanceId();
	res["projectId"] = projectId;
	res["projectRevision"] = static_cast<qint64>(applyRes.newRevision);
	res["operationKey"] = operationKey;

	QJsonObject data;
	data["status"] = applyRes.status;
	data["createdObjects"] = applyRes.createdObjects;
	data["addedNotes"] = applyRes.addedNotes;
	data["undoGroupId"] = applyRes.undoGroupId;
	res["data"] = data;

	return res;
}

QJsonObject AiApplicationFacade::undo(uint64_t expectedRevision, const QString& expectedUndoGroupId)
{

	if (Engine::getSong() && (!Engine::getSong()->isStopped() || Engine::getSong()->isExporting()))
	{
		return failure(ErrorCode::Busy, "Stop playback before changing history.");
	}
	QJsonObject res;
	ProjectJournal* pj = Engine::projectJournal();
	if (!pj || expectedUndoGroupId.isEmpty() || pj->currentUndoGroupId() != expectedUndoGroupId)
	{
		return {{"ok", false}, {"error", DomainError{ErrorCode::HistoryConflict, "History head changed."}.toJson()}};
	}

	if (!pj || !pj->canUndo())
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::HistoryConflict, "Cannot undo: journal is empty or no undo available."}.toJson();
		return res;
	}

	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (currentRev != expectedRevision)
	{
		res["ok"] = false;
		DomainError err{ErrorCode::RevisionConflict, "Revision conflict on undo."};
		QJsonObject details;
		details["expectedRevision"] = static_cast<qint64>(expectedRevision);
		details["actualRevision"] = static_cast<qint64>(currentRev);
		err.details = details;
		res["error"] = err.toJson();
		return res;
	}

	AiProjectRevision::instance()->beginEdit();
	pj->undo();
	AiObjectRegistry::instance()->invalidateAll();
	AiProjectRevision::instance()->endEdit(true);

	res["ok"] = true;
	res["newRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::redo(uint64_t expectedRevision, const QString& expectedUndoGroupId)
{

	if (Engine::getSong() && (!Engine::getSong()->isStopped() || Engine::getSong()->isExporting()))
	{
		return failure(ErrorCode::Busy, "Stop playback before changing history.");
	}
	QJsonObject res;
	ProjectJournal* pj = Engine::projectJournal();
	if (!pj || expectedUndoGroupId.isEmpty() || pj->currentRedoGroupId() != expectedUndoGroupId)
	{
		return {{"ok", false}, {"error", DomainError{ErrorCode::HistoryConflict, "History head changed."}.toJson()}};
	}

	if (!pj || !pj->canRedo())
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::HistoryConflict, "Cannot redo: no redo available."}.toJson();
		return res;
	}

	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (currentRev != expectedRevision)
	{
		res["ok"] = false;
		DomainError err{ErrorCode::RevisionConflict, "Revision conflict on redo."};
		QJsonObject details;
		details["expectedRevision"] = static_cast<qint64>(expectedRevision);
		details["actualRevision"] = static_cast<qint64>(currentRev);
		err.details = details;
		res["error"] = err.toJson();
		return res;
	}

	AiProjectRevision::instance()->beginEdit();
	pj->redo();
	AiObjectRegistry::instance()->invalidateAll();
	AiProjectRevision::instance()->endEdit(true);

	res["ok"] = true;
	res["newRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::createInstrumentTrack(const QString& pluginKey, const QString& trackName)
{
	QJsonObject res;
	Song* song = Engine::getSong();
	if (!song)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Song is not available."}.toJson();
		return res;
	}

	if (!song->isStopped() || song->isExporting())
	{
		return failure(ErrorCode::Busy, "Stop playback before creating an instrument.");
	}
	const auto catalog = listInstruments("internal", {}, 500)["instruments"].toArray();
	bool allowed = false;
	for (const auto& entry : catalog)
	{
		allowed |= entry.toObject()["key"].toString() == pluginKey;
	}
	if (!allowed) { return failure(ErrorCode::PluginUnavailable, "Instrument is not an available internal adapter."); }
	const QString undoGroup = "undo_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
	return runFacadeEdit(song, song, "Create instrument track", undoGroup, [&]() -> QJsonObject {
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		if (!track) { return failure(ErrorCode::InternalError, "Failed to instantiate InstrumentTrack."); }
		track->loadInstrument(pluginKey);
		if (!track->instrument() || QString::fromUtf8(track->instrument()->descriptor()->name) != pluginKey)
		{
			return failure(ErrorCode::PluginLoadFailed, "Instrument did not load.");
		}
		if (!trackName.isEmpty()) { track->setName(trackName); }
		return {{"ok", true}, {"trackId", AiObjectRegistry::instance()->registerTrack(track)}};
	});
}

QJsonObject AiApplicationFacade::listAssets(const QString& rootId, const QString& cursor, int limit)
{
	if (rootId != "samples" && rootId != "presets")
	{
		return failure(ErrorCode::ValidationError, "rootId must be samples or presets.");
	}
	const auto assets = AiFilePolicy::instance()->listAssets(rootId);
	const QString scope = "assets:" + rootId + ":"
		+ QString::fromLatin1(
			QCryptographicHash::hash(QJsonDocument(assets).toJson(), QCryptographicHash::Sha256).toHex());
	const int start = cursorIndex(cursor, scope);
	if (start < 0) { return failure(ErrorCode::StaleCursor, "Catalog changed."); }
	limit = qBound(1, limit, static_cast<int>(MAX_PAGE_LIMIT));
	QJsonArray page;
	int index = start;
	for (; index < assets.size() && page.size() < limit; ++index)
	{
		page.append(assets[index]);
	}
	QJsonObject result{{"ok", true}, {"items", page}};
	if (index < assets.size()) { result["nextCursor"] = pageCursor(index, scope); }
	return result;
}

QJsonObject AiApplicationFacade::loadAsset(const QString& assetId, const QString& targetTrackId)
{
	QString path;
	auto error = AiFilePolicy::instance()->resolveAsset(assetId, path);
	if (error.hasError()) { return {{"ok", false}, {"error", error.toJson()}}; }
	auto* track = dynamic_cast<InstrumentTrack*>(AiObjectRegistry::instance()->lookupTrack(targetTrackId));
	auto* song = Engine::getSong();
	if (!track) { return failure(ErrorCode::StaleHandle, "Target must be an instrument track."); }
	if (!song->isStopped() || song->isExporting())
	{
		return failure(ErrorCode::Busy, "Stop playback before loading an asset.");
	}
	const bool preset = QFileInfo(path).suffix().compare("xpf", Qt::CaseInsensitive) == 0;
	QDomDocument document;
	QDomElement element;
	if (preset)
	{
		DataFile validation(path, false);
		if (!validation.lastLoadError().isEmpty())
		{
			return failure(ErrorCode::ValidationError, "Malformed preset XML.");
		}
		if (validation.hasLocalPlugins())
		{
			return failure(ErrorCode::PermissionDenied, "Preset contains local plugin paths.");
		}
		document = validation;
		element = validation.content().firstChildElement("instrumenttrack");
	}
	else
	{
		auto sample = SampleBuffer::fromFile(path);
		if (!sample || sample->empty()) { return failure(ErrorCode::ValidationError, "Sample cannot be decoded."); }
		auto root = document.createElement("instrumenttrack");
		document.appendChild(root);
		auto instrument = document.createElement("instrument");
		instrument.setAttribute("name", "audiofileprocessor");
		root.appendChild(instrument);
		auto settings = document.createElement("audiofileprocessor");
		settings.setAttribute("src", path);
		instrument.appendChild(settings);
		element = root;
	}
	QString plugin = element.firstChildElement("instrument").attribute("name");
	const auto allowed = listInstruments("internal", {}, 500)["instruments"].toArray();
	if (element.firstChildElement("instrument").isNull())
	{
		// Older bundled presets store the instrument settings directly on the track.
		for (const auto& value : allowed)
		{
			const auto key = value.toObject()["key"].toString();
			if (!element.firstChildElement(key).isNull())
			{
				if (!plugin.isEmpty()) { return failure(ErrorCode::ValidationError, "Ambiguous preset instruments."); }
				plugin = key;
			}
		}
	}
	bool found = false;
	for (const auto& value : allowed)
	{
		found |= value.toObject()["key"].toString() == plugin;
	}
	if (element.isNull() || !found)
	{
		return failure(ErrorCode::PluginUnavailable, "Preset instrument is not an available internal adapter.");
	}
	const QString groupId = "undo_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
	return runFacadeEdit(song, track, "Load catalog asset", groupId, [&]() -> QJsonObject {
		QDomDocument presetDocument;
		auto wrapper = presetDocument.createElement("track");
		presetDocument.appendChild(wrapper);
		wrapper.appendChild(presetDocument.importNode(element, true));
		track->loadPreset(wrapper);
		if (!track->instrument() || QString::fromUtf8(track->instrument()->descriptor()->name) != plugin)
		{
			return failure(ErrorCode::PluginLoadFailed, "Instrument loading failed; original track restored.");
		}
		return {{"ok", true}};
	});
}

QJsonObject AiApplicationFacade::saveProjectCopy(const QString& fileName)
{
	QJsonObject res;
	Song* song = Engine::getSong();
	if (!song)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Song is not available."}.toJson();
		return res;
	}

	QString resolvedPath;
	DomainError err
		= AiFilePolicy::instance()->validateAndResolve(AiFilePolicy::RootType::Exports, fileName, resolvedPath, false);
	if (err.hasError())
	{
		res["ok"] = false;
		res["error"] = err.toJson();
		return res;
	}

	if (!fileName.endsWith(".mmp", Qt::CaseInsensitive))
	{
		return failure(ErrorCode::ValidationError, "Project copies require .mmp extension.");
	}
	QTemporaryDir temporary(QFileInfo(resolvedPath).dir().filePath(".lmms-copy-XXXXXX"));
	if (!temporary.isValid()) { return failure(ErrorCode::InternalError, "Cannot prepare copy."); }
	const QString staging = temporary.filePath("project.mmp");
	bool ok = song->saveProjectFile(staging, false, false)
		&& !AiFilePolicy::instance()->publishNewFile(staging, fileName, resolvedPath).hasError();
	if (!ok)
	{
		res["ok"] = false;
		res["error"]
			= DomainError{ErrorCode::InternalError, QString("Failed to write project copy to: %1").arg(resolvedPath)}
				  .toJson();
		return res;
	}

	res["ok"] = true;
	res["savedPath"] = resolvedPath;
	res["projectRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::startRender(
	const QString& outputFileName, const QString& format, int sampleRate, int bitDepth)
{
	QJsonObject res;
	QString jobId;
	DomainError err = AiJobManager::instance()->startRenderJob(outputFileName, format, sampleRate, bitDepth, jobId);
	if (err.hasError())
	{
		res["ok"] = false;
		res["error"] = err.toJson();
		return res;
	}

	res["ok"] = true;
	res["jobId"] = jobId;
	return res;
}

QJsonObject AiApplicationFacade::getJob(const QString& jobId) const
{
	QJsonObject res;
	JobInfo info;
	if (!AiJobManager::instance()->getJob(jobId, info))
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::ValidationError, QString("Job not found: %1").arg(jobId)}.toJson();
		return res;
	}

	res["ok"] = true;
	res["job"] = info.toJson();
	return res;
}

QJsonObject AiApplicationFacade::cancelJob(const QString& jobId)
{
	QJsonObject res;
	DomainError err = AiJobManager::instance()->cancelJob(jobId);
	if (err.hasError())
	{
		res["ok"] = false;
		res["error"] = err.toJson();
		return res;
	}

	res["ok"] = true;
	res["jobId"] = jobId;
	return res;
}

} // namespace lmms::ai
