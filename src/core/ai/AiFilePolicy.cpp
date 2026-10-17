#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <io.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#endif
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include "AiFilePolicy.h"

namespace lmms::ai {

AiFilePolicy* AiFilePolicy::instance()
{
	static AiFilePolicy s_instance;
	return &s_instance;
}

AiFilePolicy::AiFilePolicy()
{
	// Defaults to standard document/music directories
	QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	m_presetsDir = QDir::cleanPath(docs + "/LMMS/presets");
	m_projectsDir = QDir::cleanPath(docs + "/LMMS/projects");
	m_librariesDir = QDir::cleanPath(docs + "/LMMS/samples");
	m_exportsDir = QDir::cleanPath(docs + "/LMMS/exports");
	setAllowedDirectory(RootType::Projects, m_projectsDir);
	setAllowedDirectory(RootType::Libraries, m_librariesDir);
	setAllowedDirectory(RootType::Presets, m_presetsDir);
	setAllowedDirectory(RootType::Exports, m_exportsDir);
}

void AiFilePolicy::setAllowedDirectory(RootType type, const QString& path)
{
	QMutexLocker locker(&m_mutex);
	QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
	QStringList suffix;
	while (!info.exists() && !info.fileName().isEmpty())
	{
		suffix.prepend(info.fileName());
		info = QFileInfo(info.absolutePath());
	}
	QString clean
		= path.trimmed().isEmpty() ? QString() : QDir::cleanPath(info.canonicalFilePath() + "/" + suffix.join('/'));
	switch (type)
	{
	case RootType::Presets:
		m_presetsDir = clean;
		break;
	case RootType::Projects:
		m_projectsDir = clean;
		break;
	case RootType::Libraries:
		m_librariesDir = clean;
		break;
	case RootType::Exports:
		m_exportsDir = clean;
		break;
	}
}

QString AiFilePolicy::allowedDirectory(RootType type) const
{
	QMutexLocker locker(&m_mutex);
	switch (type)
	{
	case RootType::Presets:
		return m_presetsDir;
	case RootType::Projects:
		return m_projectsDir;
	case RootType::Libraries:
		return m_librariesDir;
	case RootType::Exports:
		return m_exportsDir;
	}
	return QString();
}

bool AiFilePolicy::isSafeFileName(const QString& name) const
{
	if (name.isEmpty() || name.length() > 255 || name.endsWith(' ') || name.endsWith('.')) { return false; }

	// Check for traversal or illegal separators
	if (name.contains("..") || name.contains('/') || name.contains('\\')) { return false; }

	// Check for Windows Alternate Data Streams
	if (name.contains(':')) { return false; }

	// Windows reserved device names
	static const QStringList reservedNames = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
		"COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

	QString baseName = name.section('.', 0, 0).toUpper();
	if (reservedNames.contains(baseName)) { return false; }

	// Reject control characters
	for (const QChar& ch : name)
	{
		if (ch.unicode() < 32 || QString("<>\"|?*").contains(ch)) { return false; }
	}

	return true;
}

bool AiFilePolicy::isPathAllowed(const QString& fullPath, RootType requiredRoot) const
{
	QString root = allowedDirectory(requiredRoot);
	if (root.isEmpty()) { return false; }

	QFileInfo rootInfo(root);
	QString canonicalRoot = rootInfo.exists() ? rootInfo.canonicalFilePath() : QDir::cleanPath(root);
#ifdef Q_OS_WIN
	if (canonicalRoot.compare(QDir::cleanPath(root), Qt::CaseInsensitive) != 0) { return false; }
#else
	if (canonicalRoot != QDir::cleanPath(root)) { return false; }
#endif

	QFileInfo pathInfo(fullPath);
	QString canonicalTarget;
	if (pathInfo.exists()) { canonicalTarget = pathInfo.canonicalFilePath(); }
	else
	{
		// Target doesn't exist yet; canonicalize parent directory
		QFileInfo parentInfo(pathInfo.absolutePath());
		if (!parentInfo.exists()) { return false; }
		canonicalTarget = QDir::cleanPath(parentInfo.canonicalFilePath() + "/" + pathInfo.fileName());
	}

// Ensure target is inside canonicalRoot (case-insensitive on Windows)
#ifdef Q_OS_WIN
	return canonicalTarget.startsWith(canonicalRoot + "/", Qt::CaseInsensitive);
#else
	return canonicalTarget.startsWith(canonicalRoot + "/", Qt::CaseSensitive);
#endif
}

DomainError AiFilePolicy::validateAndResolve(
	RootType rootType, const QString& relativeOrFileName, QString& outResolvedCanonicalPath, bool mustExist) const
{
	DomainError err;
	if (relativeOrFileName.isEmpty())
	{
		err.code = ErrorCode::ValidationError;
		err.message = "File path or name cannot be empty.";
		return err;
	}

	QString root = allowedDirectory(rootType);
	QDir rootDir(root);
	if (!rootDir.exists())
	{
		// Attempt to create root if it's the export directory
		if (rootType == RootType::Exports) { rootDir.mkpath("."); }
		else if (mustExist)
		{
			err.code = ErrorCode::PathNotAllowed;
			err.message = QString("Allowed root directory does not exist: %1").arg(root);
			return err;
		}
	}

	QString cleanRelative = QDir::cleanPath(relativeOrFileName);
	for (const auto& component : cleanRelative.split('/'))
	{
		if (!isSafeFileName(component)) { return {ErrorCode::PathNotAllowed, "Unsafe path component."}; }
	}
	if (cleanRelative.startsWith("/") || cleanRelative.startsWith("\\") || cleanRelative.contains(":"))
	{
		err.code = ErrorCode::PathNotAllowed;
		err.message = "Absolute paths or drive letters are not allowed in relative filenames.";
		return err;
	}

	if (cleanRelative.contains(".."))
	{
		err.code = ErrorCode::PathNotAllowed;
		err.message = "Directory traversal ('..') is strictly prohibited.";
		return err;
	}

	QString fullPath = QDir::cleanPath(root + "/" + cleanRelative);
	if (!isPathAllowed(fullPath, rootType))
	{
		err.code = ErrorCode::PathNotAllowed;
		err.message = QString("Target path '%1' escapes the allowed root directory.").arg(relativeOrFileName);
		return err;
	}

	QFileInfo fileInfo(fullPath);
	if (!mustExist && (fileInfo.exists() || fileInfo.isSymLink()))
	{
		return {ErrorCode::PathNotAllowed, "Destination already exists."};
	}
	if (mustExist && !fileInfo.exists())
	{
		err.code = ErrorCode::AssetNotFound;
		err.message = QString("File not found: %1").arg(relativeOrFileName);
		return err;
	}

	outResolvedCanonicalPath = fileInfo.exists()
		? fileInfo.canonicalFilePath()
		: QDir(QFileInfo(fileInfo.absolutePath()).canonicalFilePath()).filePath(fileInfo.fileName());
	return err;
}

QJsonArray AiFilePolicy::listAssets(const QString& rootId)
{
	const auto root = rootId == "presets" ? RootType::Presets : RootType::Libraries;
	const QString directory = allowedDirectory(root);
	QDirIterator iterator(directory,
		root == RootType::Presets ? QStringList{"*.xpf"} : QStringList{"*.wav", "*.ogg", "*.flac"},
		QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
	QJsonArray items;
	while (iterator.hasNext() && items.size() < 2000)
	{
		const QString file = iterator.next();
		const QFileInfo info(file);
		if (!isPathAllowed(file, root) || info.size() > 64 * 1024 * 1024) { continue; }
		const auto canonical = info.canonicalFilePath();
		const auto modified = info.lastModified().toMSecsSinceEpoch();
		const QString id = "asset_"
			+ QString::fromLatin1(QCryptographicHash::hash(
				(canonical + QString::number(info.size()) + ":" + QString::number(modified)).toUtf8(),
				QCryptographicHash::Sha256)
					.toHex());
		{
			QMutexLocker lock(&m_mutex);
			if (m_assets.size() >= 4096 && !m_assets.contains(id)) { break; }
			m_assets.insert(id, {canonical, root, info.size(), modified});
		}
		items.append(QJsonObject{{"assetId", id}, {"name", info.fileName()}, {"rootId", rootId},
			{"type", root == RootType::Presets ? "preset" : "sample"}});
	}
	return items;
}
DomainError AiFilePolicy::resolveAsset(const QString& assetId, QString& path) const
{
	Asset asset;
	{
		QMutexLocker lock(&m_mutex);
		if (!m_assets.contains(assetId))
		{
			return {ErrorCode::AssetNotFound, "Unknown catalog asset; list assets first."};
		}
		asset = m_assets.value(assetId);
	}
	const QFileInfo info(asset.path);
	if (!info.isFile() || info.isSymLink() || !isPathAllowed(asset.path, asset.root) || info.size() != asset.size
		|| info.lastModified().toMSecsSinceEpoch() != asset.modified)
	{
		return {ErrorCode::AssetNotFound, "Catalog asset changed or is unavailable."};
	}
	path = asset.path;
	return {};
}

DomainError AiFilePolicy::publishNewFile(const QString& source, const QString& fileName, QString& destination) const
{
	if (!isSafeFileName(fileName)) { return {ErrorCode::PathNotAllowed, "Exports require a plain file name."}; }
	auto error = validateAndResolve(RootType::Exports, fileName, destination, false);
	if (error.hasError()) { return error; }
	QFile input(source);
	if (!input.open(QIODevice::ReadOnly)) { return {ErrorCode::InternalError, "Cannot read staged export."}; }
#ifdef Q_OS_WIN
	HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(destination.utf16()), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
		nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return {ErrorCode::PathNotAllowed, "Export destination exists or is unavailable."};
	}
	auto discard = [&] {
		FILE_DISPOSITION_INFO info{TRUE};
		SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info));
		CloseHandle(handle);
	};
	std::wstring finalName(32768, L'\0');
	const auto length = GetFinalPathNameByHandleW(
		handle, finalName.data(), static_cast<DWORD>(finalName.size()), FILE_NAME_NORMALIZED);
	if (!length || length >= finalName.size())
	{
		discard();
		return {ErrorCode::PathNotAllowed, "Cannot verify export handle."};
	}
	finalName.resize(length);
	QString actual = QDir::fromNativeSeparators(QString::fromStdWString(finalName));
	if (actual.startsWith("//?/UNC/", Qt::CaseInsensitive)) { actual = "//" + actual.mid(8); }
	else if (actual.startsWith("//?/")) { actual.remove(0, 4); }
	if (QDir::cleanPath(actual).compare(destination, Qt::CaseInsensitive) != 0)
	{
		discard();
		return {ErrorCode::PathNotAllowed, "Export directory changed while opening the destination."};
	}
	while (!input.atEnd())
	{
		const auto bytes = input.read(64 * 1024);
		DWORD written = 0;
		if (bytes.isEmpty()
			|| !WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr)
			|| written != bytes.size())
		{
			discard();
			return {ErrorCode::InternalError, "Export write failed."};
		}
	}
	if (!FlushFileBuffers(handle))
	{
		discard();
		return {ErrorCode::InternalError, "Export flush failed."};
	}
	CloseHandle(handle);
#else
	const QString root = allowedDirectory(RootType::Exports);
	const int directory = ::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory < 0) { return {ErrorCode::PathNotAllowed, "Cannot open export directory."}; }
	char actualName[PATH_MAX + 1]{};
#ifdef __APPLE__
	const bool found = fcntl(directory, F_GETPATH, actualName) == 0;
#else
	const auto link = QByteArray("/proc/self/fd/") + QByteArray::number(directory);
	const auto length = readlink(link.constData(), actualName, PATH_MAX);
	const bool found = length > 0 && length < PATH_MAX;
#endif
	if (!found || QDir::cleanPath(QFile::decodeName(actualName)) != QDir::cleanPath(root))
	{
		::close(directory);
		return {ErrorCode::PathNotAllowed, "Export directory changed."};
	}
	const auto name = QFile::encodeName(fileName);
	const int output = openat(directory, name.constData(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (output < 0)
	{
		::close(directory);
		return {ErrorCode::PathNotAllowed, "Export destination exists or is unavailable."};
	}
	bool success = true;
	while (!input.atEnd())
	{
		auto bytes = input.read(64 * 1024);
		qint64 offset = 0;
		if (bytes.isEmpty())
		{
			success = false;
			break;
		}
		while (offset < bytes.size())
		{
			const auto written = ::write(output, bytes.constData() + offset, bytes.size() - offset);
			if (written <= 0)
			{
				success = false;
				break;
			}
			offset += written;
		}
		if (!success) { break; }
	}
	success = success && fsync(output) == 0;
	::close(output);
	if (!success) { unlinkat(directory, name.constData(), 0); }
	::close(directory);
	if (!success) { return {ErrorCode::InternalError, "Export write failed."}; }
#endif
	return {};
}

} // namespace lmms::ai
