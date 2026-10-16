#ifndef LMMS_AI_EDIT_TRANSACTION_H
#define LMMS_AI_EDIT_TRANSACTION_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>

#include "AiChangePlan.h"
#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms {
class Song;
}

namespace lmms::ai {

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
		if (error.hasError()) { o["error"] = error.toJson(); }
		return o;
	}
};

class LMMS_EXPORT AiEditTransaction
{
public:
	static DomainError validateCommands(const std::vector<ChangeCommand>& commands);
	// The optional observer is an internal fault-injection seam; it is never accepted over IPC.
	static ApplyResult executePlan(Song* song, std::shared_ptr<AiChangePlan> plan, const QString& operationKey,
		qint64 deadlineEpochMs = 0, const std::function<void(int)>& afterCommand = {});
};

} // namespace lmms::ai

#endif // LMMS_AI_EDIT_TRANSACTION_H
