#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "AiProjectSnapshot.h"
namespace lmms::ai {
DomainError AiProjectSnapshot::materializeResources(const QString& manifestFile)
{
	QFile manifestInput(manifestFile);
	if (!manifestInput.open(QIODevice::ReadOnly) || manifestInput.size() > 1024 * 1024)
	{
		return {ErrorCode::AssetNotFound, "Snapshot manifest unavailable or too large."};
	}
	auto manifest = QJsonDocument::fromJson(manifestInput.readAll()).object();
	manifestInput.close();
	if (!manifest["resources"].isArray() || manifest["snapshotId"].toString().isEmpty())
	{
		return {ErrorCode::ValidationError, "Invalid snapshot manifest."};
	}
	const QString root = QFileInfo(manifestFile).dir().canonicalPath();
	const QDir targetDirectory(root + "/resources");
	if (QFileInfo(targetDirectory.path()).isSymLink()
		|| targetDirectory.canonicalPath() != targetDirectory.absolutePath())
	{
		return {ErrorCode::PathNotAllowed, "Snapshot resource directory changed."};
	}
	if (manifest["resources"].toArray().size() > 2000)
	{
		return {ErrorCode::ValidationError, "Too many snapshot resources."};
	}
	qint64 totalBytes = 0;
	QJsonArray verified;
	for (const auto& value : manifest["resources"].toArray())
	{
		auto entry = value.toObject();
		const QString name = entry["fileName"].toString();
		if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(":") || name.contains(".."))
		{
			return {ErrorCode::PathNotAllowed, "Invalid snapshot resource name."};
		}
		QFileInfo info(entry["sourcePath"].toString());
		if (!info.isFile() || info.size() != entry["bytes"].toVariant().toLongLong()
			|| info.lastModified().toMSecsSinceEpoch() != entry["modifiedAt"].toVariant().toLongLong())
		{
			return {ErrorCode::AssetNotFound, "A resource changed after the snapshot was prepared."};
		}
		totalBytes += info.size();
		if (info.size() > 64 * 1024 * 1024 || totalBytes > 256 * 1024 * 1024)
		{
			return {ErrorCode::ValidationError, "Snapshot resource budget exceeded."};
		}
		QFile source(info.canonicalFilePath());
		QCryptographicHash original(QCryptographicHash::Sha256);
		if (!source.open(QIODevice::ReadOnly) || !original.addData(&source))
		{
			return {ErrorCode::AssetNotFound, "Cannot read snapshot resource."};
		}
		source.close();
		const auto digest = original.result();
		const QString target = targetDirectory.filePath(name);
		if (!QFile::exists(target) && !QFile::copy(info.canonicalFilePath(), target))
		{
			return {ErrorCode::InternalError, "Cannot copy snapshot resource."};
		}
		QFile copied(target);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!copied.open(QIODevice::ReadOnly) || !hash.addData(&copied) || hash.result() != digest)
		{
			return {ErrorCode::ExportValidationFailed, "Resource changed while copying."};
		}
		entry["sha256"] = QString::fromLatin1(digest.toHex());
		entry["snapshotPath"] = target;
		entry.remove("sourcePath");
		verified.append(entry);
	}
	manifest["resources"] = verified;
	manifest["materialized"] = true;
	QSaveFile output(manifestFile);
	const auto bytes = QJsonDocument(manifest).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
	{
		return {ErrorCode::InternalError, "Cannot finalize resource manifest."};
	}
	return {};
}

} // namespace lmms::ai
