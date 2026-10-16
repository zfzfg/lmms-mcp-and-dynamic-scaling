#ifndef LMMS_AI_EDIT_TRANSACTION_H
#define LMMS_AI_EDIT_TRANSACTION_H

#include <QString>
#include <QJsonObject>
#include <QHash>
#include <memory>
#include "lmms_export.h"
#include "AiDomainTypes.h"
#include "AiChangePlan.h"

namespace lmms
{
class Song;
}

namespace lmms::ai
{

struct LMMS_EXPORT ApplyResult
{
	bool ok{false};
	QString status{"committed"};
	uint64_t newRevision{0};
	QString undoGroupId;
	QJsonObject createdObjects;
	int addedNotes{0};
	DomainError error;

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["ok"] = ok;
		o["status"] = status;
		o["newRevision"] = static_cast<qint64>(newRevision);
		o["undoGroupId"] = undoGroupId;
		o["createdObjects"] = createdObjects;
		o["addedNotes"] = addedNotes;
		if (error.hasError())
		{
			o["error"] = error.toJson();
		}
		return o;
	}
};

class LMMS_EXPORT AiEditTransaction
{
public:
	static ApplyResult executePlan(Song* song,
	                              std::shared_ptr<AiChangePlan> plan,
	                              const QString& operationKey);
};

} // namespace lmms::ai

#endif // LMMS_AI_EDIT_TRANSACTION_H
