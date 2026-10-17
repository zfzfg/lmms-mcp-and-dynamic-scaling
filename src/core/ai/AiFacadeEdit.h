#ifndef LMMS_AI_FACADE_EDIT_H
#define LMMS_AI_FACADE_EDIT_H

#include "AiDomainTypes.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms::ai {

// Keep rollback inside the revision scope, including restoration notifications.
template<typename Edit>
QJsonObject runFacadeEdit(Song* song, JournallingObject* checkpoint, const QString& name,
	const QString& groupId, Edit edit)
{
	auto* journal = Engine::projectJournal();
	if (journal->isGroupOpen())
	{
		return {{"ok", false}, {"error", DomainError{ErrorCode::Busy, "Journal busy."}.toJson()}};
	}
	const bool dirty = song->isModified();
	const bool journalling = journal->isJournalling();
	auto* revision = AiProjectRevision::instance();
	revision->beginEdit();
	QJsonObject result;
	bool committed = false;
	bool restored = true;
	try
	{
		journal->beginJournalGroup(name, groupId);
		journal->setJournalling(true);
		journal->addJournalCheckPoint(checkpoint);
		journal->setJournalling(false);
		result = edit();
		if (result["ok"].toBool())
		{
			journal->endJournalGroup();
			song->setModified();
			committed = true;
		}
	}
	catch (...)
	{
		result = {{"ok", false}, {"error", DomainError{ErrorCode::InternalError,
			"Instrument or asset edit failed after an exception."}.toJson()}};
	}
	if (!committed)
	{
		try { journal->cancelJournalGroup(); }
		catch (...)
		{
			restored = false;
			result = {{"ok", false}, {"error", DomainError{ErrorCode::InternalError,
				"Edit failed and rollback could not restore the project completely."}.toJson()}};
		}
		AiObjectRegistry::instance()->invalidateAll();
		song->setModified(restored ? dirty : true);
	}
	journal->setJournalling(journalling);
	revision->endEdit(committed || !restored);
	if (committed)
	{
		result["undoGroupId"] = groupId;
		result["newRevision"] = static_cast<qint64>(revision->currentRevision());
	}
	return result;
}

} // namespace lmms::ai

#endif
