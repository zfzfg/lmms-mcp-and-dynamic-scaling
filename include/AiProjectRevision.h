#ifndef LMMS_AI_PROJECT_REVISION_H
#define LMMS_AI_PROJECT_REVISION_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <atomic>
#include <cstdint>

#include "lmms_export.h"

namespace lmms {
class Song;
class TrackContainer;
} // namespace lmms

namespace lmms::ai {

class LMMS_EXPORT AiProjectRevision : public QObject
{
	Q_OBJECT
public:
	static AiProjectRevision* instance();

	QString projectId() const;
	uint64_t currentRevision() const;
	uint64_t currentClipRevision(const QString& clipHandle) const;

	void bumpRevision(const QString& reason = QString());
	void bumpClipRevision(const QString& clipHandle);
	void resetProject(const QString& newProjectId = QString());

	void attachToSong(Song* song);
	void detach();
	void beginEdit() { ++m_editDepth; }
	void endEdit(bool changed);

signals:
	void revisionChanged(uint64_t newRevision, const QString& reason);

public slots:
	void onModelModified();
	void onParameterModified();
	void onTrackStructureChanged();

private:
	AiProjectRevision();
	~AiProjectRevision() override = default;

	mutable QMutex m_mutex;
	QString m_projectId;
	uint64_t m_revision{1};
	QHash<QString, uint64_t> m_clipRevisions;

	Song* m_attachedSong{nullptr};
	int m_editDepth{0};
	std::atomic_bool m_notificationPending{false};
};

} // namespace lmms::ai

#endif // LMMS_AI_PROJECT_REVISION_H
