#include "AiFilePolicy.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace lmms::ai
{

AiFilePolicy* AiFilePolicy::instance()
{
	static AiFilePolicy s_instance;
	return &s_instance;
}

AiFilePolicy::AiFilePolicy()
{
	// Defaults to standard document/music directories
	QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	m_projectsDir = QDir::cleanPath(docs + "/LMMS/projects");
	m_librariesDir = QDir::cleanPath(docs + "/LMMS/samples");
	m_exportsDir = QDir::cleanPath(docs + "/LMMS/exports");
}

void AiFilePolicy::setAllowedDirectory(RootType type, const QString& path)
{
	QMutexLocker locker(&m_mutex);
	QString clean = QDir::cleanPath(path);
	switch (type)
	{
		case RootType::Projects: m_projectsDir = clean; break;
		case RootType::Libraries: m_librariesDir = clean; break;
		case RootType::Exports: m_exportsDir = clean; break;
	}
}

QString AiFilePolicy::allowedDirectory(RootType type) const
{
	QMutexLocker locker(&m_mutex);
	switch (type)
	{
		case RootType::Projects: return m_projectsDir;
		case RootType::Libraries: return m_librariesDir;
		case RootType::Exports: return m_exportsDir;
	}
	return QString();
}

bool AiFilePolicy::isSafeFileName(const QString& name) const
{
	if (name.isEmpty() || name.length() > 255) { return false; }

	// Check for traversal or illegal separators
	if (name.contains("..") || name.contains('/') || name.contains('\\'))
	{
		return false;
	}

	// Check for Windows Alternate Data Streams
	if (name.contains(':'))
	{
		return false;
	}

	// Windows reserved device names
	static const QStringList reservedNames = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};

	QString baseName = QFileInfo(name).completeBaseName().toUpper();
	if (reservedNames.contains(baseName))
	{
		return false;
	}

	// Reject control characters
	for (const QChar& ch : name)
	{
		if (ch.unicode() < 32)
		{
			return false;
		}
	}

	return true;
}

bool AiFilePolicy::isPathAllowed(const QString& fullPath, RootType requiredRoot) const
{
	QString root = allowedDirectory(requiredRoot);
	if (root.isEmpty()) { return false; }

	QFileInfo rootInfo(root);
	QString canonicalRoot = rootInfo.exists() ? rootInfo.canonicalFilePath() : QDir::cleanPath(root);

	QFileInfo pathInfo(fullPath);
	QString canonicalTarget;
	if (pathInfo.exists())
	{
		canonicalTarget = pathInfo.canonicalFilePath();
	}
	else
	{
		// Target doesn't exist yet; canonicalize parent directory
		QFileInfo parentInfo(pathInfo.absolutePath());
		if (!parentInfo.exists())
		{
			return false;
		}
		canonicalTarget = QDir::cleanPath(parentInfo.canonicalFilePath() + "/" + pathInfo.fileName());
	}

	// Ensure target is inside canonicalRoot (case-insensitive on Windows)
	#ifdef Q_OS_WIN
	return canonicalTarget.startsWith(canonicalRoot, Qt::CaseInsensitive);
	#else
	return canonicalTarget.startsWith(canonicalRoot, Qt::CaseSensitive);
	#endif
}

DomainError AiFilePolicy::validateAndResolve(RootType rootType,
                                             const QString& relativeOrFileName,
                                             QString& outResolvedCanonicalPath,
                                             bool mustExist) const
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
		if (rootType == RootType::Exports)
		{
			rootDir.mkpath(".");
		}
		else if (mustExist)
		{
			err.code = ErrorCode::PathNotAllowed;
			err.message = QString("Allowed root directory does not exist: %1").arg(root);
			return err;
		}
	}

	QString cleanRelative = QDir::cleanPath(relativeOrFileName);
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
	if (mustExist && !fileInfo.exists())
	{
		err.code = ErrorCode::AssetNotFound;
		err.message = QString("File not found: %1").arg(relativeOrFileName);
		return err;
	}

	outResolvedCanonicalPath = fileInfo.exists() ? fileInfo.canonicalFilePath() : fullPath;
	return err;
}

} // namespace lmms::ai
