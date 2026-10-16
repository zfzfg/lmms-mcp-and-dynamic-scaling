#ifndef LMMS_AI_FILE_POLICY_H
#define LMMS_AI_FILE_POLICY_H

#include <QString>
#include <QStringList>
#include <QMutex>
#include <QDir>
#include "lmms_export.h"
#include "AiDomainTypes.h"

namespace lmms::ai
{

class LMMS_EXPORT AiFilePolicy
{
public:
	static AiFilePolicy* instance();

	enum class RootType
	{
		Projects,
		Libraries,
		Exports
	};

	void setAllowedDirectory(RootType type, const QString& path);
	QString allowedDirectory(RootType type) const;

	bool isSafeFileName(const QString& name) const;
	bool isPathAllowed(const QString& fullPath, RootType requiredRoot) const;

	DomainError validateAndResolve(RootType rootType,
	                               const QString& relativeOrFileName,
	                               QString& outResolvedCanonicalPath,
	                               bool mustExist = false) const;

private:
	AiFilePolicy();
	mutable QMutex m_mutex;

	QString m_projectsDir;
	QString m_librariesDir;
	QString m_exportsDir;
};

} // namespace lmms::ai

#endif // LMMS_AI_FILE_POLICY_H
