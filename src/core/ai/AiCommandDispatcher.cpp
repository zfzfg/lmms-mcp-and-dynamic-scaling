#include "AiCommandDispatcher.h"
#include "AiApplicationFacade.h"
#include <QMetaObject>

namespace lmms::ai
{

AiCommandDispatcher* AiCommandDispatcher::instance()
{
	static AiCommandDispatcher s_instance;
	return &s_instance;
}

AiCommandDispatcher::AiCommandDispatcher()
	: QObject(nullptr)
{
	connect(this, &AiCommandDispatcher::commandEnqueued,
	        this, &AiCommandDispatcher::processNextCommand,
	        Qt::QueuedConnection);
}

void AiCommandDispatcher::dispatch(qint64 requestId,
                                  const QString& action,
                                  const QJsonObject& payload,
                                  const QString& operationKey,
                                  int deadlineMs,
                                  Permission grantedPermissions,
                                  std::function<void(const QJsonObject& response)> callback)
{
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	qint64 deadline = now + (deadlineMs > 0 ? deadlineMs : DEFAULT_DEADLINE_MS);

	// Check permissions
	bool requiresWrite = (action == "apply_changes" || action == "create_instrument_track" ||
	                      action == "load_asset" || action == "set_transport" ||
	                      action == "undo" || action == "redo");
	if (requiresWrite && !hasPermission(grantedPermissions, Permission::WriteProject))
	{
		QJsonObject errRes;
		errRes["ok"] = false;
		errRes["error"] = DomainError{ErrorCode::PermissionDenied, "Action requires 'write_project' permission."}.toJson();
		if (callback) { callback(errRes); }
		return;
	}

	bool requiresExport = (action == "save_project_copy" || action == "start_render");
	if (requiresExport && !hasPermission(grantedPermissions, Permission::Export))
	{
		QJsonObject errRes;
		errRes["ok"] = false;
		errRes["error"] = DomainError{ErrorCode::PermissionDenied, "Action requires 'export' permission."}.toJson();
		if (callback) { callback(errRes); }
		return;
	}

	// Deduplication check
	if (!operationKey.isEmpty())
	{
		QMutexLocker locker(&m_mutex);
		if (m_operations.contains(operationKey))
		{
			const auto& op = m_operations.value(operationKey);
			if (op.action == action && op.requestPayload == payload)
			{
				if (callback) { callback(op.responseData); }
				return;
			}
			else
			{
				QJsonObject errRes;
				errRes["ok"] = false;
				errRes["error"] = DomainError{ErrorCode::OperationKeyReused, "Operation key reused with conflicting arguments."}.toJson();
				if (callback) { callback(errRes); }
				return;
			}
		}
	}

	{
		QMutexLocker locker(&m_mutex);
		if (m_queue.size() >= static_cast<int>(MAX_PENDING_COMMANDS))
		{
			QJsonObject errRes;
			errRes["ok"] = false;
			errRes["error"] = DomainError{ErrorCode::Busy, "Command dispatcher queue is full."}.toJson();
			if (callback) { callback(errRes); }
			return;
		}

		QueuedCommand cmd;
		cmd.requestId = requestId;
		cmd.action = action;
		cmd.payload = payload;
		cmd.operationKey = operationKey;
		cmd.deadlineEpochMs = deadline;
		cmd.callback = callback;

		m_queue.enqueue(cmd);
	}

	emit commandEnqueued();
}

bool AiCommandDispatcher::lookupOperation(const QString& opKey, QJsonObject& outResult) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_operations.contains(opKey))
	{
		return false;
	}
	outResult = m_operations.value(opKey).responseData;
	return true;
}

void AiCommandDispatcher::processNextCommand()
{
	QueuedCommand cmd;
	{
		QMutexLocker locker(&m_mutex);
		if (m_queue.isEmpty())
		{
			return;
		}
		cmd = m_queue.dequeue();
	}

	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now > cmd.deadlineEpochMs)
	{
		QJsonObject errRes;
		errRes["ok"] = false;
		errRes["error"] = DomainError{ErrorCode::Timeout, "Command exceeded deadline before execution."}.toJson();
		if (cmd.callback) { cmd.callback(errRes); }
		return;
	}

	QJsonObject res;
	auto facade = AiApplicationFacade::instance();

	if (cmd.action == "get_status")
	{
		res = facade->getStatus();
	}
	else if (cmd.action == "get_project")
	{
		res = facade->getProject(cmd.payload["projectId"].toString());
	}
	else if (cmd.action == "list_tracks")
	{
		res = facade->listTracks(cmd.payload["container"].toString(),
		                         cmd.payload["cursor"].toString(),
		                         cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
	}
	else if (cmd.action == "list_clips")
	{
		res = facade->listClips(cmd.payload["trackId"].toString(),
		                        static_cast<tick_t>(cmd.payload["startTick"].toVariant().toLongLong()),
		                        static_cast<tick_t>(cmd.payload["endTick"].toVariant().toLongLong()),
		                        cmd.payload["cursor"].toString(),
		                        cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
	}
	else if (cmd.action == "get_notes")
	{
		res = facade->getNotes(cmd.payload["clipId"].toString(),
		                       static_cast<tick_t>(cmd.payload["startTick"].toVariant().toLongLong()),
		                       static_cast<tick_t>(cmd.payload["endTick"].toVariant().toLongLong()),
		                       cmd.payload["cursor"].toString(),
		                       cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
	}
	else if (cmd.action == "list_parameters")
	{
		res = facade->listParameters(cmd.payload["ownerId"].toString(),
		                             cmd.payload["cursor"].toString(),
		                             cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
	}
	else if (cmd.action == "list_instruments")
	{
		res = facade->listInstruments(cmd.payload["type"].toString(),
		                              cmd.payload["cursor"].toString(),
		                              cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
	}
	else if (cmd.action == "get_mixer")
	{
		res = facade->getMixer(cmd.payload["channelIndex"].toInt(-1));
	}
	else if (cmd.action == "set_transport")
	{
		res = facade->setTransport(cmd.payload["action"].toString(),
		                           static_cast<tick_t>(cmd.payload["tickPosition"].toVariant().toLongLong()));
	}
	else if (cmd.action == "preview_changes")
	{
		res = facade->previewChanges(cmd.payload["projectId"].toString(),
		                             cmd.payload["expectedRevision"].toVariant().toULongLong(),
		                             cmd.payload["changes"].toArray());
	}
	else if (cmd.action == "apply_changes")
	{
		res = facade->applyChanges(cmd.payload["projectId"].toString(),
		                           cmd.payload["expectedRevision"].toVariant().toULongLong(),
		                           cmd.payload["planId"].toString(),
		                           cmd.operationKey);
	}
	else if (cmd.action == "undo")
	{
		res = facade->undo(cmd.payload["expectedRevision"].toVariant().toULongLong(),
		                   cmd.payload["undoGroupId"].toString());
	}
	else if (cmd.action == "redo")
	{
		res = facade->redo(cmd.payload["expectedRevision"].toVariant().toULongLong(),
		                   cmd.payload["undoGroupId"].toString());
	}
	else if (cmd.action == "create_instrument_track")
	{
		res = facade->createInstrumentTrack(cmd.payload["pluginKey"].toString(),
		                                    cmd.payload["trackName"].toString());
	}
	else if (cmd.action == "load_asset")
	{
		res = facade->loadAsset(cmd.payload["assetId"].toString(),
		                        cmd.payload["targetTrackId"].toString());
	}
	else if (cmd.action == "save_project_copy")
	{
		res = facade->saveProjectCopy(cmd.payload["fileName"].toString());
	}
	else if (cmd.action == "start_render")
	{
		res = facade->startRender(cmd.payload["outputFileName"].toString(),
		                          cmd.payload["format"].toString("wav"),
		                          cmd.payload["sampleRate"].toInt(44100),
		                          cmd.payload["bitDepth"].toInt(16));
	}
	else if (cmd.action == "get_job")
	{
		res = facade->getJob(cmd.payload["jobId"].toString());
	}
	else if (cmd.action == "cancel_job")
	{
		res = facade->cancelJob(cmd.payload["jobId"].toString());
	}
	else if (cmd.action == "get_operation")
	{
		QJsonObject opRes;
		if (lookupOperation(cmd.payload["operationKey"].toString(), opRes))
		{
			res["ok"] = true;
			res["data"] = opRes;
		}
		else
		{
			res["ok"] = false;
			res["error"] = DomainError{ErrorCode::ValidationError, "Operation key not found."}.toJson();
		}
	}
	else
	{
		res["ok"] = false;
		res["error"] = DomainError{ErrorCode::UnsupportedOperation, QString("Unknown action: %1").arg(cmd.action)}.toJson();
	}

	// Register completed operation if key provided
	if (!cmd.operationKey.isEmpty() && res["ok"].toBool(false))
	{
		QMutexLocker locker(&m_mutex);
		OperationRecord rec;
		rec.operationKey = cmd.operationKey;
		rec.action = cmd.action;
		rec.requestPayload = cmd.payload;
		rec.responseData = res;
		rec.completedAtEpochMs = QDateTime::currentMSecsSinceEpoch();

		m_operations.insert(cmd.operationKey, rec);
		m_operationHistory.append(cmd.operationKey);

		if (m_operationHistory.size() > MAX_OPERATION_HISTORY)
		{
			QString oldest = m_operationHistory.takeFirst();
			m_operations.remove(oldest);
		}
	}

	if (cmd.callback)
	{
		cmd.callback(res);
	}
}

} // namespace lmms::ai
