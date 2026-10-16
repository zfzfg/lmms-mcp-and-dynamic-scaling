#include "AiProjectRevision.h"
#include "Song.h"
#include "TrackContainer.h"
#include <QUuid>

namespace lmms::ai
{

AiProjectRevision* AiProjectRevision::instance()
{
	static AiProjectRevision s_instance;
	return &s_instance;
}

AiProjectRevision::AiProjectRevision()
	: QObject(nullptr)
{
	resetProject();
}

QString AiProjectRevision::projectId() const
{
	QMutexLocker locker(&m_mutex);
	return m_projectId;
}

uint64_t AiProjectRevision::currentRevision() const
{
	QMutexLocker locker(&m_mutex);
	return m_revision;
}

uint64_t AiProjectRevision::currentClipRevision(const QString& clipHandle) const
{
	QMutexLocker locker(&m_mutex);
	return m_clipRevisions.value(clipHandle, 1);
}

void AiProjectRevision::bumpRevision(const QString& reason)
{
	uint64_t newRev = 0;
	{
		QMutexLocker locker(&m_mutex);
		m_revision++;
		newRev = m_revision;
	}
	emit revisionChanged(newRev, reason);
}

void AiProjectRevision::bumpClipRevision(const QString& clipHandle)
{
	QMutexLocker locker(&m_mutex);
	m_clipRevisions[clipHandle] = m_clipRevisions.value(clipHandle, 1) + 1;
}

void AiProjectRevision::resetProject(const QString& newProjectId)
{
	uint64_t newRev = 1;
	{
		QMutexLocker locker(&m_mutex);
		if (newProjectId.isEmpty())
		{
			m_projectId = QString("proj_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
		}
		else
		{
			m_projectId = newProjectId;
		}
		m_revision = 1;
		m_clipRevisions.clear();
	}
	emit revisionChanged(newRev, "project_reset");
}

void AiProjectRevision::attachToSong(Song* song)
{
	detach();
	m_attachedSong = song;
	if (m_attachedSong)
	{
		connect(m_attachedSong, SIGNAL(trackAdded(lmms::Track*)), this, SLOT(onTrackStructureChanged()));
		connect(m_attachedSong, SIGNAL(trackRemoved()), this, SLOT(onTrackStructureChanged()));
		connect(m_attachedSong, SIGNAL(trackMoved()), this, SLOT(onTrackStructureChanged()));
	}
}

void AiProjectRevision::detach()
{
	if (m_attachedSong)
	{
		disconnect(m_attachedSong, nullptr, this, nullptr);
		m_attachedSong = nullptr;
	}
}

void AiProjectRevision::onModelModified()
{
	bumpRevision("model_modified");
}

void AiProjectRevision::onTrackStructureChanged()
{
	bumpRevision("track_structure_changed");
}

} // namespace lmms::ai
