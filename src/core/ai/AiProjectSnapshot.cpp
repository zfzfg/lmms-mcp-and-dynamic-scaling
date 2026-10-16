#include "AiProjectSnapshot.h"
#include "Song.h"
#include "Engine.h"
#include "AudioEngine.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace lmms::ai
{

DomainError AiProjectSnapshot::createSnapshot(Song* song,
                                              uint64_t revision,
                                              const QString& targetTempDir,
                                              SnapshotMetadata& outMetadata)
{
	DomainError err;
	if (!song)
	{
		err.code = ErrorCode::NotReady;
		err.message = "Song is not available.";
		return err;
	}

	QString snapshotId = QString("snap_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	QString dirPath = targetTempDir.isEmpty()
		? QDir::tempPath() + "/lmms_snapshot_" + snapshotId
		: targetTempDir;

	QDir dir(dirPath);
	if (!dir.exists())
	{
		dir.mkpath(".");
	}

	QString snapshotFile = dir.filePath("project.mmp");
	QString manifestFile = dir.filePath("manifest.json");

	bool saveOk = false;
	{
		// Protect snapshot saving with RAII model lock
		auto guard = Engine::audioEngine()->requestChangesGuard();
		saveOk = song->saveProjectFile(snapshotFile, false);
	}

	if (!saveOk || !QFile::exists(snapshotFile))
	{
		err.code = ErrorCode::InternalError;
		err.message = "Failed to write project snapshot file.";
		return err;
	}

	// Write manifest
	QJsonObject manifestObj;
	manifestObj["snapshotId"] = snapshotId;
	manifestObj["projectRevision"] = static_cast<qint64>(revision);
	manifestObj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	QFile mf(manifestFile);
	if (mf.open(QIODevice::WriteOnly))
	{
		mf.write(QJsonDocument(manifestObj).toJson());
		mf.close();
	}

	outMetadata.snapshotId = snapshotId;
	outMetadata.projectRevision = revision;
	outMetadata.snapshotFilePath = snapshotFile;
	outMetadata.manifestFilePath = manifestFile;
	outMetadata.tempDirectory = dirPath;

	return err;
}

void AiProjectSnapshot::cleanupSnapshot(const SnapshotMetadata& metadata)
{
	if (!metadata.tempDirectory.isEmpty())
	{
		QDir dir(metadata.tempDirectory);
		if (dir.exists())
		{
			dir.removeRecursively();
		}
	}
}

} // namespace lmms::ai
