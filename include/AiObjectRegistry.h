#ifndef LMMS_AI_OBJECT_REGISTRY_H
#define LMMS_AI_OBJECT_REGISTRY_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QHash>
#include <QMutex>
#include <cstdint>
#include <memory>

#include "lmms_export.h"
#include "AiDomainTypes.h"

namespace lmms
{
class Track;
class Clip;
class MidiClip;
class Note;
class MixerChannel;
class AutomatableModel;
}

namespace lmms::ai
{

class LMMS_EXPORT AiObjectRegistry : public QObject
{
	Q_OBJECT
public:
	static AiObjectRegistry* instance();

	uint64_t currentGeneration() const { return m_generation; }
	void invalidateAll(); // Call on project clear / reload

	// Track mapping
	QString registerTrack(Track* track);
	Track* lookupTrack(const QString& handle) const;
	QString findTrackHandle(Track* track) const;

	// Clip mapping
	QString registerClip(Clip* clip);
	Clip* lookupClip(const QString& handle) const;
	MidiClip* lookupMidiClip(const QString& handle) const;
	QString findClipHandle(Clip* clip) const;

	// Note mapping (clip-revision scoped)
	QString generateNoteRef(const QString& clipHandle, uint64_t clipRev, int noteIndex);
	bool parseNoteRef(const QString& noteRef, QString& outClipHandle, uint64_t& outClipRev, int& outNoteIndex) const;

	// MixerChannel mapping
	QString registerMixerChannel(int channelIndex);
	int lookupMixerChannel(const QString& handle) const;

	// Parameter mapping
	QString registerParameter(const QString& ownerId, const QString& paramKey);

private:
	AiObjectRegistry();
	~AiObjectRegistry() override = default;

	mutable QMutex m_mutex;
	uint64_t m_generation{1};

	// Generational maps
	QHash<QString, QPointer<Track>> m_tracksByHandle;
	QHash<Track*, QString> m_handlesByTrack;

	QHash<QString, QPointer<Clip>> m_clipsByHandle;
	QHash<Clip*, QString> m_handlesByClip;

	QHash<QString, int> m_mixerChannelByHandle;
	QHash<int, QString> m_handlesByMixerChannel;

	uint64_t m_nextTrackId{1};
	uint64_t m_nextClipId{1};
};

} // namespace lmms::ai

#endif // LMMS_AI_OBJECT_REGISTRY_H
