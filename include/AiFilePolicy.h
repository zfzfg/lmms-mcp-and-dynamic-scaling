#ifndef LMMS_AI_FILE_POLICY_H
#define LMMS_AI_FILE_POLICY_H

#include <QDir>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms::ai {

class LMMS_EXPORT AiFilePolicy
{
public:
	static AiFilePolicy* instance();

	enum class RootType
	{
		Projects,
		Libraries,
		Presets,
		Exports
	};

	void setAllowedDirectory(RootType type, const QString& path);
	QString allowedDirectory(RootType type) const;

	QJsonArray listAssets(const QString& rootId);
	DomainError publishNewFile(const QString& source, const QString& fileName, QString& destination) const;
	DomainError resolveAsset(const QString& assetId, QString& path) const;

	bool isSafeFileName(const QString& name) const;
	bool isPathAllowed(const QString& fullPath, RootType requiredRoot) const;

	DomainError validateAndResolve(RootType rootType, const QString& relativeOrFileName,
		QString& outResolvedCanonicalPath, bool mustExist = false) const;

private:
	AiFilePolicy();
	mutable QMutex m_mutex;

	struct Asset
	{
		QString path;
		RootType root;
		qint64 size;
		qint64 modified;
	};
	QHash<QString, Asset> m_assets;
	QString m_presetsDir;
	QString m_projectsDir;
	QString m_librariesDir;
	QString m_exportsDir;
};

} // namespace lmms::ai

#endif // LMMS_AI_FILE_POLICY_H
