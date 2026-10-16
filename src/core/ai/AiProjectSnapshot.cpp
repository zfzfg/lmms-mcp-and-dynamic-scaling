#include "AiProjectSnapshot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

#include "AiProjectRevision.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "PathUtil.h"
#include "Song.h"

namespace lmms::ai {

DomainError AiProjectSnapshot::createSnapshot(
	Song* song, uint64_t revision, const QString& targetTempDir, SnapshotMetadata& outMetadata)
{
	DomainError err;
	if (!song)
	{
		err.code = ErrorCode::NotReady;
		err.message = "Song is not available.";
		return err;
	}

	QString snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QTemporaryDir temporary(QDir::tempPath() + "/lmms_snapshot_XXXXXX");
	if (!temporary.isValid()) { return {ErrorCode::InternalError, "Cannot create snapshot directory."}; }
	if (!targetTempDir.isEmpty())
	{
		return {ErrorCode::ValidationError, "Custom snapshot directories are not supported."};
	}
	QString dirPath = temporary.path();
	QDir dir(dirPath);

	QString snapshotFile = dir.filePath("project.mmp");
	QString manifestFile = dir.filePath("manifest.json");

	const bool saveOk = song->saveProjectFile(snapshotFile, false, false);

	if (!saveOk || !QFile::exists(snapshotFile))
	{
		err.code = ErrorCode::InternalError;
		err.message = "Failed to write project snapshot file.";
		return err;
	}

	QFile project(snapshotFile);
	QDomDocument document;
	if (!project.open(QIODevice::ReadOnly) || !document.setContent(&project))
	{
		return {ErrorCode::InternalError, "Cannot read snapshot."};
	}
	project.close();
	QJsonArray resources;
	qint64 totalBytes = 0;
	if (!dir.mkdir("resources")) { return {ErrorCode::InternalError, "Cannot create resource directory."}; }
	for (const auto& tag : {QString("sampleclip"), QString("audiofileprocessor")})
	{
		auto elements = document.elementsByTagName(tag);
		for (int i = 0; i < elements.size(); ++i)
		{
			auto element = elements.at(i).toElement();
			const QString source = element.attribute("src");
			if (source.isEmpty()) { continue; }
			bool failed = false;
			const QString absolute = PathUtil::toAbsolute(source, &failed);
			QFileInfo info(absolute);
			totalBytes += info.size();
			if (failed || !info.isFile() || info.size() > 64 * 1024 * 1024 || totalBytes > 256 * 1024 * 1024
				|| resources.size() >= 2000)
			{
				return {ErrorCode::AssetNotFound, "A snapshot resource is missing or exceeds the resource budget."};
			}
			const QString name
				= QString::fromLatin1(
					  QCryptographicHash::hash(info.canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex())
				+ "." + info.suffix();
			element.setAttribute("src", dir.filePath("resources/" + name));
			resources.append(QJsonObject{{"sourcePath", info.canonicalFilePath()}, {"fileName", name},
				{"bytes", info.size()}, {"modifiedAt", info.lastModified().toMSecsSinceEpoch()}});
		}
	}

	QSaveFile frozen(snapshotFile);
	const auto xml = document.toByteArray();
	if (!frozen.open(QIODevice::WriteOnly) || frozen.write(xml) != xml.size() || !frozen.commit())
	{
		return {ErrorCode::InternalError, "Cannot finalize snapshot."};
	}
	// Write manifest
	QJsonObject manifestObj;
	manifestObj["snapshotId"] = snapshotId;
	manifestObj["projectId"] = AiProjectRevision::instance()->projectId();
	manifestObj["resources"] = resources;
	manifestObj["projectRevision"] = static_cast<qint64>(revision);
	manifestObj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	QSaveFile mf(manifestFile);
	const auto manifest = QJsonDocument(manifestObj).toJson();
	if (!mf.open(QIODevice::WriteOnly) || mf.write(manifest) != manifest.size() || !mf.commit())
	{
		return {ErrorCode::InternalError, "Cannot write resource manifest."};
	}

	temporary.setAutoRemove(false);
	outMetadata.projectId = AiProjectRevision::instance()->projectId();
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
		if (dir.exists() && !QFileInfo(metadata.tempDirectory).isSymLink()
			&& QFileInfo(metadata.tempDirectory).absolutePath() == QDir::tempPath()
			&& QFileInfo(metadata.tempDirectory).fileName().startsWith("lmms_snapshot_"))
		{
			dir.removeRecursively();
		}
	}
}

} // namespace lmms::ai
