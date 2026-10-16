#include "AiApplicationFacade.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "AiChangePlan.h"
#include "AiEditTransaction.h"
#include "AiParameterRegistry.h"
#include "AiFilePolicy.h"
#include "AiJobManager.h"
#include "AiControlService.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include <QUuid>

namespace lmms::ai
{

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
	res["permissions"] = permissionsToStringList(AiControlService::instance()->grantedPermissions());
	res["busy"] = false;

	QJsonObject caps;
	caps["editMidi"] = true;
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
		res["error"] = DomainError{ErrorCode::ProjectChanged, "Project ID does not match currently active document."}.toJson();
		return res;
	}

	ProjectSummary summary;
	summary.projectId = projId;
	summary.projectRevision = AiProjectRevision::instance()->currentRevision();
	summary.name = song->projectFileName().isEmpty() ? "Unbenannt" : song->projectFileName();
	summary.modified = song->isModified();
	summary.tempoBpm = song->getTempo();
	summary.timeSigNumerator = song->getTimeSigModel().numerator();
	summary.timeSigDenominator = song->getTimeSigModel().denominator();
	summary.ticksPerQuarter = 48;
	summary.ticksPerBar = song->getTimeSigModel().ticksPerBar();
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
	TrackContainer* tc = (container == "pattern_store") ? static_cast<TrackContainer*>(Engine::patternStore()) : static_cast<TrackContainer*>(Engine::getSong());
	if (!tc)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::NotReady, "Requested container is not available."}.toJson();
		return res;
	}

	int parsedCursor = 0;
	if (!cursor.isEmpty())
	{
		parsedCursor = cursor.toInt();
	}

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
			ts.instrumentKey = it->instrumentName();
		}

		items.append(ts.toJson());
		count++;
		nextIdx = static_cast<int>(i + 1);
	}

	res["ok"] = true;
	res["items"] = items;
	if (nextIdx < static_cast<int>(tracks.size()))
	{
		res["nextCursor"] = QString::number(nextIdx);
	}
	return res;
}

QJsonObject AiApplicationFacade::listClips(const QString& trackId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const
{
	QJsonObject res;
	Track* t = AiObjectRegistry::instance()->lookupTrack(trackId);
	if (!t)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::StaleHandle, QString("Track not found for handle: %1").arg(trackId)}.toJson();
		return res;
	}

	int parsedCursor = 0;
	if (!cursor.isEmpty()) { parsedCursor = cursor.toInt(); }
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	if (limit > static_cast<int>(MAX_PAGE_LIMIT)) { limit = MAX_PAGE_LIMIT; }

	const auto& clips = t->getClips();
	QJsonArray items;
	int count = 0;
	int nextIdx = parsedCursor;

	for (int i = parsedCursor; i < clips.size() && count < limit; ++i)
	{
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
		else
		{
			cs.type = "other";
		}

		items.append(cs.toJson());
		count++;
		nextIdx = i + 1;
	}

	res["ok"] = true;
	res["items"] = items;
	if (nextIdx < clips.size())
	{
		res["nextCursor"] = QString::number(nextIdx);
	}
	return res;
}

QJsonObject AiApplicationFacade::getNotes(const QString& clipId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const
{
	QJsonObject res;
	MidiClip* mc = AiObjectRegistry::instance()->lookupMidiClip(clipId);
	if (!mc)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::StaleHandle, QString("MidiClip not found for handle: %1").arg(clipId)}.toJson();
		return res;
	}

	uint64_t clipRev = AiProjectRevision::instance()->currentClipRevision(clipId);

	int parsedCursor = 0;
	if (!cursor.isEmpty()) { parsedCursor = cursor.toInt(); }
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	if (limit > static_cast<int>(MAX_PAGE_LIMIT)) { limit = MAX_PAGE_LIMIT; }

	const auto& notes = mc->notes();
	QJsonArray items;
	int count = 0;
	int nextIdx = parsedCursor;

	for (int i = parsedCursor; i < notes.size() && count < limit; ++i)
	{
		const Note* n = notes[i];
		if (endTick > 0 && (n->pos().getTicks() > endTick || (n->pos().getTicks() + n->length().getTicks()) < startTick))
		{
			continue;
		}

		NoteData nd;
		nd.noteRef = AiObjectRegistry::instance()->generateNoteRef(clipId, clipRev, i);
		nd.positionTicks = n->pos().getTicks();
		nd.lengthTicks = n->length().getTicks();
		nd.midiKey = n->key();
		nd.volumePercent = n->volume();
		nd.pan = n->panning();
		nd.noteType = (n->type() == Note::Type::Step) ? "step" : "regular";

		items.append(nd.toJson());
		count++;
		nextIdx = i + 1;
	}

	res["ok"] = true;
	res["clipId"] = clipId;
	res["clipRevision"] = static_cast<qint64>(clipRev);
	res["notes"] = items;
	if (nextIdx < notes.size())
	{
		res["nextCursor"] = QString::number(nextIdx);
	}
	return res;
}

QJsonObject AiApplicationFacade::listParameters(const QString& ownerId, const QString& cursor, int limit) const
{
	QJsonObject res;
	if (limit <= 0) { limit = DEFAULT_PAGE_LIMIT; }
	auto params = AiParameterRegistry::instance()->listParameters(ownerId, cursor, limit);
	QJsonArray arr;
	for (const auto& p : params)
	{
		arr.append(p.toJson());
	}
	res["ok"] = true;
	res["parameters"] = arr;
	return res;
}

QJsonObject AiApplicationFacade::listInstruments(const QString& typeFilter, const QString& cursor, int limit) const
{
	Q_UNUSED(typeFilter);
	Q_UNUSED(cursor);
	Q_UNUSED(limit);
	QJsonObject res;
	QJsonArray arr;

	// Internal instrument descriptors
	static const QStringList builtins = {
		"tripleoscillator", "bitinvader", "organic", "kicker", "audiofileprocessor", "monstro", "watsyn", "vibedrum"
	};

	for (const auto& key : builtins)
	{
		QJsonObject inst;
		inst["key"] = key;
		inst["name"] = key.toUpper();
		inst["isInternal"] = true;
		inst["freigabeStatus"] = "allowed";
		arr.append(inst);
	}

	res["ok"] = true;
	res["instruments"] = arr;
	return res;
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

		channelsArr.append(mcs.toJson());
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
		song->playSong();
	}
	else if (action == "pause")
	{
		song->pauseSong();
	}
	else if (action == "stop")
	{
		song->stopSong();
	}
	else if (action == "seek")
	{
		song->setPlayPos(seekTicks);
	}
	else
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::ValidationError, QString("Unknown transport action: %1").arg(action)}.toJson();
		return res;
	}

	res["ok"] = true;
	res["action"] = action;
	res["isPlaying"] = song->isPlaying();
	res["isPaused"] = song->isPaused();
	res["positionTicks"] = static_cast<qint64>(song->getPlayPos().getTicks());
	return res;
}

QJsonObject AiApplicationFacade::previewChanges(const QString& projectId, uint64_t expectedRevision, const QJsonArray& changes)
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
		err.message = QString("Project revision conflict: expected %1 but current is %2.").arg(expectedRevision).arg(currentRev);
		QJsonObject details;
		details["expectedRevision"] = static_cast<qint64>(expectedRevision);
		details["actualRevision"] = static_cast<qint64>(currentRev);
		err.details = details;
		res["error"] = err.toJson();
		return res;
	}

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

	QString planId = QString("plan_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	auto plan = std::make_shared<AiChangePlan>(planId, projectId, expectedRevision, cmds);
	AiChangePlanManager::instance()->storePlan(plan);

	res["ok"] = true;
	res["plan"] = plan->toJson();
	return res;
}

QJsonObject AiApplicationFacade::applyChanges(const QString& projectId, uint64_t expectedRevision, const QString& planId, const QString& operationKey)
{
	QJsonObject res;
	auto plan = AiChangePlanManager::instance()->lookupPlan(planId);
	if (!plan)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::PlanExpired, QString("Plan '%1' not found or expired.").arg(planId)}.toJson();
		return res;
	}

	ApplyResult applyRes = AiEditTransaction::executePlan(Engine::getSong(), plan, operationKey);
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
	Q_UNUSED(expectedUndoGroupId);
	QJsonObject res;
	ProjectJournal* pj = Engine::projectJournal();
	if (!pj || !pj->canUndo())
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::HistoryConflict, "Cannot undo: journal is empty or no undo available."}.toJson();
		return res;
	}

	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (expectedRevision > 0 && currentRev != expectedRevision)
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

	pj->undo();
	AiProjectRevision::instance()->bumpRevision("undo");

	res["ok"] = true;
	res["newRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::redo(uint64_t expectedRevision, const QString& expectedUndoGroupId)
{
	Q_UNUSED(expectedUndoGroupId);
	QJsonObject res;
	ProjectJournal* pj = Engine::projectJournal();
	if (!pj || !pj->canRedo())
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::HistoryConflict, "Cannot redo: no redo available."}.toJson();
		return res;
	}

	uint64_t currentRev = AiProjectRevision::instance()->currentRevision();
	if (expectedRevision > 0 && currentRev != expectedRevision)
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

	pj->redo();
	AiProjectRevision::instance()->bumpRevision("redo");

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

	Track* t = Track::create(Track::Type::Instrument, song);
	auto it = dynamic_cast<InstrumentTrack*>(t);
	if (!it)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::InternalError, "Failed to instantiate InstrumentTrack."}.toJson();
		return res;
	}

	it->loadInstrument(pluginKey);
	if (!trackName.isEmpty())
	{
		it->setName(trackName);
	}

	QString handle = AiObjectRegistry::instance()->registerTrack(it);
	AiProjectRevision::instance()->bumpRevision("create_instrument_track");

	res["ok"] = true;
	res["trackId"] = handle;
	res["newRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::loadAsset(const QString& assetId, const QString& targetTrackId)
{
	QJsonObject res;
	QString resolvedPath;
	DomainError err = AiFilePolicy::instance()->validateAndResolve(AiFilePolicy::RootType::Libraries, assetId, resolvedPath, true);
	if (err.hasError())
	{
		res["ok"] = false;
		res["error"] = err.toJson();
		return res;
	}

	Track* t = AiObjectRegistry::instance()->lookupTrack(targetTrackId);
	if (!t)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::StaleHandle, QString("Track handle not found: %1").arg(targetTrackId)}.toJson();
		return res;
	}

	// Load preset into track
	QDomDocument doc;
	QFile f(resolvedPath);
	if (f.open(QIODevice::ReadOnly))
	{
		doc.setContent(&f);
		f.close();
		t->loadSettings(doc.documentElement());
		AiProjectRevision::instance()->bumpRevision("load_asset");
		res["ok"] = true;
		res["newRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
		return res;
	}

	res["ok"] = false;
	res["error"] = DomainError{ErrorCode::InternalError, QString("Failed to read asset XML: %1").arg(resolvedPath)}.toJson();
	return res;
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
	DomainError err = AiFilePolicy::instance()->validateAndResolve(AiFilePolicy::RootType::Exports, fileName, resolvedPath, false);
	if (err.hasError())
	{
		res["ok"] = false;
		res["error"] = err.toJson();
		return res;
	}

	bool ok = song->saveProjectFile(resolvedPath, false);
	if (!ok)
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::InternalError, QString("Failed to write project copy to: %1").arg(resolvedPath)}.toJson();
		return res;
	}

	res["ok"] = true;
	res["savedPath"] = resolvedPath;
	res["projectRevision"] = static_cast<qint64>(AiProjectRevision::instance()->currentRevision());
	return res;
}

QJsonObject AiApplicationFacade::startRender(const QString& outputFileName, const QString& format, int sampleRate, int bitDepth)
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
