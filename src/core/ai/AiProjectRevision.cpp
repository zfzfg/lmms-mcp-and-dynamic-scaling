#include "AiProjectRevision.h"

#include <QThread>
#include <QUuid>

#include "AiChangePlan.h"
#include "AiObjectRegistry.h"
#include "Song.h"
#include "TrackContainer.h"

namespace lmms::ai {

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
	return m_revision + m_clipRevisions.value(clipHandle, 0);
}

void AiProjectRevision::bumpRevision(const QString& reason)
{
	if (m_editDepth) { return; }
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
	m_clipRevisions[clipHandle] = m_clipRevisions.value(clipHandle, 0) + 1;
}

void AiProjectRevision::resetProject(const QString& newProjectId)
{
	AiObjectRegistry::instance()->invalidateAll();
	AiChangePlanManager::instance()->clear();
	uint64_t newRev = 1;
	{
		QMutexLocker locker(&m_mutex);
		if (newProjectId.isEmpty())
		{
			m_projectId = QString("proj_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		}
		else { m_projectId = newProjectId; }
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
		connect(&song->tempoModel(), SIGNAL(dataChanged()), this, SLOT(onParameterModified()));
		connect(&song->getTimeSigModel(), SIGNAL(dataChanged()), this, SLOT(onParameterModified()));
	}
}

void AiProjectRevision::detach()
{
	if (m_attachedSong)
	{
		disconnect(m_attachedSong, nullptr, this, nullptr);
		disconnect(&m_attachedSong->tempoModel(), nullptr, this, nullptr);
		disconnect(&m_attachedSong->getTimeSigModel(), nullptr, this, nullptr);
		m_attachedSong = nullptr;
	}
}

void AiProjectRevision::endEdit(bool changed)
{
	if (m_editDepth > 0) { --m_editDepth; }
	if (!m_editDepth && changed) { bumpRevision("edit_completed"); }
}

void AiProjectRevision::onParameterModified()
{
	if (m_attachedSong && !m_attachedSong->isStopped()) { return; }
	onModelModified();
}

void AiProjectRevision::onModelModified()
{
	if (QThread::currentThread() != thread())
	{
		if (!m_notificationPending.exchange(true))
		{
			QMetaObject::invokeMethod(
				this,
				[this] {
					m_notificationPending = false;
					onModelModified();
				},
				Qt::QueuedConnection);
		}
		return;
	}
	bumpRevision("model_modified");
}

void AiProjectRevision::onTrackStructureChanged()
{
	bumpRevision("track_structure_changed");
}

} // namespace lmms::ai
