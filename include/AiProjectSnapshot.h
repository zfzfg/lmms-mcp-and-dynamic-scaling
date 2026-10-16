#ifndef LMMS_AI_PROJECT_SNAPSHOT_H
#define LMMS_AI_PROJECT_SNAPSHOT_H

#include <QString>
#include <QStringList>
#include <cstdint>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms {
class Song;
}

namespace lmms::ai {

struct LMMS_EXPORT SnapshotMetadata
{
	QString snapshotId;
	QString projectId;
	uint64_t projectRevision{0};
	QString snapshotFilePath;
	QString manifestFilePath;
	QString tempDirectory;
};

class LMMS_EXPORT AiProjectSnapshot
{
public:
	static DomainError createSnapshot(
		Song* song, uint64_t revision, const QString& targetTempDir, SnapshotMetadata& outMetadata);

	static DomainError materializeResources(const QString& manifestFile);
	static void cleanupSnapshot(const SnapshotMetadata& metadata);
};

} // namespace lmms::ai

#endif // LMMS_AI_PROJECT_SNAPSHOT_H
