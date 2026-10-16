#include "AiObjectRegistry.h"
#include "Track.h"
#include "Clip.h"
#include "MidiClip.h"

namespace lmms::ai
{

AiObjectRegistry* AiObjectRegistry::instance()
{
	static AiObjectRegistry s_instance;
	return &s_instance;
}

AiObjectRegistry::AiObjectRegistry()
	: QObject(nullptr)
{
}

void AiObjectRegistry::invalidateAll()
{
	QMutexLocker locker(&m_mutex);
	m_generation++;
	m_tracksByHandle.clear();
	m_handlesByTrack.clear();
	m_clipsByHandle.clear();
	m_handlesByClip.clear();
	m_mixerChannelByHandle.clear();
	m_handlesByMixerChannel.clear();
	m_nextTrackId = 1;
	m_nextClipId = 1;
}

QString AiObjectRegistry::registerTrack(Track* track)
{
	if (!track) { return QString(); }
	QMutexLocker locker(&m_mutex);

	if (m_handlesByTrack.contains(track))
	{
		QString handle = m_handlesByTrack.value(track);
		if (m_tracksByHandle.contains(handle) && m_tracksByHandle.value(handle) == track)
		{
			return handle;
		}
	}

	QString handle = QString("trk_%1_%2").arg(m_generation).arg(m_nextTrackId++);
	m_tracksByHandle.insert(handle, track);
	m_handlesByTrack.insert(track, handle);
	return handle;
}

Track* AiObjectRegistry::lookupTrack(const QString& handle) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_tracksByHandle.contains(handle))
	{
		return nullptr;
	}
	return m_tracksByHandle.value(handle).data();
}

QString AiObjectRegistry::findTrackHandle(Track* track) const
{
	if (!track) { return QString(); }
	QMutexLocker locker(&m_mutex);
	return m_handlesByTrack.value(track, QString());
}

QString AiObjectRegistry::registerClip(Clip* clip)
{
	if (!clip) { return QString(); }
	QMutexLocker locker(&m_mutex);

	if (m_handlesByClip.contains(clip))
	{
		QString handle = m_handlesByClip.value(clip);
		if (m_clipsByHandle.contains(handle) && m_clipsByHandle.value(handle) == clip)
		{
			return handle;
		}
	}

	QString handle = QString("clp_%1_%2").arg(m_generation).arg(m_nextClipId++);
	m_clipsByHandle.insert(handle, clip);
	m_handlesByClip.insert(clip, handle);
	return handle;
}

Clip* AiObjectRegistry::lookupClip(const QString& handle) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_clipsByHandle.contains(handle))
	{
		return nullptr;
	}
	return m_clipsByHandle.value(handle).data();
}

MidiClip* AiObjectRegistry::lookupMidiClip(const QString& handle) const
{
	Clip* c = lookupClip(handle);
	if (!c) { return nullptr; }
	return dynamic_cast<MidiClip*>(c);
}

QString AiObjectRegistry::findClipHandle(Clip* clip) const
{
	if (!clip) { return QString(); }
	QMutexLocker locker(&m_mutex);
	return m_handlesByClip.value(clip, QString());
}

QString AiObjectRegistry::generateNoteRef(const QString& clipHandle, uint64_t clipRev, int noteIndex)
{
	return QString("nt_%1_%2_%3").arg(clipHandle).arg(clipRev).arg(noteIndex);
}

bool AiObjectRegistry::parseNoteRef(const QString& noteRef, QString& outClipHandle, uint64_t& outClipRev, int& outNoteIndex) const
{
	// Expected format: nt_<clipHandle>_<clipRev>_<noteIndex>
	// where clipHandle is e.g. clp_1_2
	if (!noteRef.startsWith("nt_")) { return false; }
	QString remainder = noteRef.mid(3);
	int lastUnderscore = remainder.lastIndexOf('_');
	if (lastUnderscore <= 0) { return false; }
	bool okIndex = false;
	outNoteIndex = remainder.mid(lastUnderscore + 1).toInt(&okIndex);
	if (!okIndex) { return false; }

	QString beforeIndex = remainder.left(lastUnderscore);
	int prevUnderscore = beforeIndex.lastIndexOf('_');
	if (prevUnderscore <= 0) { return false; }
	bool okRev = false;
	outClipRev = beforeIndex.mid(prevUnderscore + 1).toULongLong(&okRev);
	if (!okRev) { return false; }

	outClipHandle = beforeIndex.left(prevUnderscore);
	return true;
}

QString AiObjectRegistry::registerMixerChannel(int channelIndex)
{
	QMutexLocker locker(&m_mutex);
	if (m_handlesByMixerChannel.contains(channelIndex))
	{
		return m_handlesByMixerChannel.value(channelIndex);
	}

	QString handle = QString("mix_%1_%2").arg(m_generation).arg(channelIndex);
	m_mixerChannelByHandle.insert(handle, channelIndex);
	m_handlesByMixerChannel.insert(channelIndex, handle);
	return handle;
}

int AiObjectRegistry::lookupMixerChannel(const QString& handle) const
{
	QMutexLocker locker(&m_mutex);
	return m_mixerChannelByHandle.value(handle, -1);
}

QString AiObjectRegistry::registerParameter(const QString& ownerId, const QString& paramKey)
{
	return QString("prm_%1_%2").arg(ownerId, paramKey);
}

} // namespace lmms::ai
