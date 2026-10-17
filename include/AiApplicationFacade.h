#ifndef LMMS_AI_APPLICATION_FACADE_H
#define LMMS_AI_APPLICATION_FACADE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms {
class Song;
}

namespace lmms::ai {

class LMMS_EXPORT AiApplicationFacade
{
public:
	static AiApplicationFacade* instance();

	// Status & Inspection
	QJsonObject getStatus() const;
	QJsonObject getProject(const QString& optProjectId = QString()) const;
	QJsonObject listTracks(const QString& container, const QString& cursor, int limit) const;
	QJsonObject listClips(
		const QString& trackId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const;
	QJsonObject getClipSummary(const QString& clipId) const;
	QJsonObject getAutomation(const QString& clipId, const QString& cursor, int limit) const;
	QJsonObject getNotes(
		const QString& clipId, tick_t startTick, tick_t endTick, const QString& cursor, int limit) const;
	QJsonObject listParameters(const QString& ownerId, const QString& cursor, int limit) const;
	QJsonObject listInstruments(const QString& typeFilter, const QString& cursor, int limit) const;
	QJsonObject getMixer(int channelIndex = -1) const;

	// Transport
	QJsonObject setTransport(const QString& action, tick_t seekTicks = 0);

	// Change plan & execution
	QJsonObject previewChanges(const QString& projectId, uint64_t expectedRevision, const QJsonArray& changes);
	QJsonObject applyChanges(const QString& projectId, uint64_t expectedRevision, const QString& planId,
		const QString& operationKey, qint64 deadlineEpochMs = 0);

	// Undo / Redo
	QJsonObject undo(uint64_t expectedRevision, const QString& expectedUndoGroupId = QString());
	QJsonObject redo(uint64_t expectedRevision, const QString& expectedUndoGroupId = QString());

	// Production & R3 features
	QJsonObject createInstrumentTrack(const QString& pluginKey, const QString& trackName);
	QJsonObject listAssets(const QString& rootId, const QString& cursor, int limit);
	QJsonObject loadAsset(const QString& assetId, const QString& targetTrackId);
	QJsonObject saveProjectCopy(const QString& fileName);

	// Rendering & Jobs
	QJsonObject startRender(const QString& outputFileName, const QString& format, int sampleRate, int bitDepth);
	QJsonObject getJob(const QString& jobId) const;
	QJsonObject cancelJob(const QString& jobId);

private:
	AiApplicationFacade() = default;
};

} // namespace lmms::ai

#endif // LMMS_AI_APPLICATION_FACADE_H
