#include "AiCommandDispatcher.h"

#include <QMetaObject>
#include <exception>

#include "AiApplicationFacade.h"
#include "AiControlService.h"
#include "AiJobManager.h"
#include "AiProjectRevision.h"
#include "AiSchemaValidator.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::ai {

AiCommandDispatcher* AiCommandDispatcher::instance()
{
	static AiCommandDispatcher s_instance;
	return &s_instance;
}

AiCommandDispatcher::AiCommandDispatcher()
	: QObject(nullptr)
{
	connect(this, &AiCommandDispatcher::commandEnqueued, this, &AiCommandDispatcher::processNextCommand,
		Qt::QueuedConnection);
}

namespace {
DomainError checkPermissions(const QString& action, Permission permissions)
{
	if (!hasPermission(permissions, Permission::Read))
	{
		return {ErrorCode::PermissionDenied, "Read permission required."};
	}
	const QStringList writing{
		"apply_changes", "create_instrument_track", "load_asset", "set_transport", "undo", "redo"};
	if (writing.contains(action) && !hasPermission(permissions, Permission::WriteProject))
	{
		return {ErrorCode::PermissionDenied, "Write permission required."};
	}
	if ((action == "load_asset" || action == "list_assets") && !hasPermission(permissions, Permission::Assets))
	{
		return {ErrorCode::PermissionDenied, "Asset permission required."};
	}
	if ((action == "start_render" || action == "save_project_copy" || action == "cancel_job" || action == "get_job"
			|| action == "read_artifact")
		&& !hasPermission(permissions, Permission::Export))
	{
		return {ErrorCode::PermissionDenied, "Export permission required."};
	}
	return {};
}
} // namespace

void AiCommandDispatcher::dispatch(qint64 requestId, const QString& action, const QJsonObject& payload,
	const QString& operationKey, int deadlineMs, Permission grantedPermissions,
	std::function<void(const QJsonObject&)> callback, std::function<bool()> connectionAlive)
{
	auto reject = [&](DomainError error) {
		if (callback) { callback({{"ok", false}, {"error", error.toJson()}}); }
	};
	const auto effective = static_cast<Permission>(static_cast<uint32_t>(grantedPermissions)
		& static_cast<uint32_t>(AiControlService::instance()->grantedPermissions()));
	const auto permissionError = checkPermissions(action, effective);
	if (permissionError.hasError())
	{
		reject(permissionError);
		return;
	}
	if (connectionAlive && !connectionAlive())
	{
		reject({ErrorCode::Cancelled, "Connection grant expired."});
		return;
	}
	if (deadlineMs <= 0)
	{
		reject({ErrorCode::Timeout, "Deadline already expired."});
		return;
	}
	const QStringList mutations{"apply_changes", "create_instrument_track", "load_asset", "set_transport", "undo",
		"redo", "start_render", "save_project_copy", "cancel_job"};
	if (mutations.contains(action) && operationKey.isEmpty())
	{
		reject({ErrorCode::ValidationError, "Mutation requires operationKey."});
		return;
	}
	QJsonObject arguments = payload;
	if (mutations.contains(action))
	{
		if (payload.contains("operationKey") && payload["operationKey"].toString() != operationKey)
		{
			reject({ErrorCode::ValidationError, "Payload and envelope operation keys differ."});
			return;
		}
		arguments["operationKey"] = operationKey;
	}
	const auto schemaError = validateToolArguments(action, arguments);
	if (schemaError.hasError())
	{
		reject(schemaError);
		return;
	}
	QMutexLocker lock(&m_mutex);
	if (!operationKey.isEmpty() && m_operations.contains(operationKey))
	{
		auto& op = m_operations[operationKey];
		if (op.action != action || op.requestPayload != payload)
		{
			lock.unlock();
			reject({ErrorCode::OperationKeyReused, "Key reused with different arguments."});
			return;
		}
		if (!op.completed)
		{
			op.waiters.append(callback);
			return;
		}
		auto response = op.responseData;
		lock.unlock();
		if (callback) { callback(response); }
		return;
	}
	if (m_queue.size() >= static_cast<int>(MAX_PENDING_COMMANDS))
	{
		lock.unlock();
		reject({ErrorCode::Busy, "Command queue full."});
		return;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	// Completed keys are guaranteed for 24 hours. Never evict active/recent operations.
	for (auto it = m_operations.begin(); it != m_operations.end();)
	{
		if (it->completed && now - it->completedAtEpochMs > 24LL * 3600 * 1000) { it = m_operations.erase(it); }
		else { ++it; }
	}
	if (!operationKey.isEmpty())
	{
		if (m_operations.size() >= MAX_OPERATION_HISTORY)
		{
			lock.unlock();
			reject({ErrorCode::Busy, "Operation register full."});
			return;
		}
		OperationRecord op;
		op.operationKey = operationKey;
		op.action = action;
		op.requestPayload = payload;
		op.responseData = {{"ok", true}, {"status", "queued"}};
		m_operations.insert(operationKey, op);
	}
	QueuedCommand command;
	command.requestId = requestId;
	command.action = action;
	command.payload = payload;
	command.operationKey = operationKey;
	command.deadlineEpochMs = now + qMin(deadlineMs, 30000);
	command.permissions = grantedPermissions;
	command.connectionAlive = connectionAlive;
	command.callback = callback;
	m_queue.enqueue(command);
	lock.unlock();
	emit commandEnqueued();
}

void AiCommandDispatcher::reset()
{
	QQueue<QueuedCommand> commands;
	{
		QMutexLocker lock(&m_mutex);
		commands.swap(m_queue);
		m_operations.clear();
	}
	for (const auto& command : commands)
		if (command.callback)
		{
			command.callback(
				{{"ok", false}, {"error", DomainError{ErrorCode::Cancelled, "Service restarted."}.toJson()}});
		}
}

bool AiCommandDispatcher::lookupOperation(const QString& opKey, QJsonObject& outResult) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_operations.contains(opKey)) { return false; }
	outResult = m_operations.value(opKey).responseData;
	return true;
}

void AiCommandDispatcher::processNextCommand()
{
	QueuedCommand cmd;
	{
		QMutexLocker locker(&m_mutex);
		if (m_queue.isEmpty()) { return; }
		cmd = m_queue.dequeue();
	}

	auto finish = [&](const QJsonObject& response) {
		AiControlService::instance()->recordResult(cmd.action, response);
		QList<std::function<void(const QJsonObject&)>> waiters;
		if (!cmd.operationKey.isEmpty())
		{
			QMutexLocker lock(&m_mutex);
			auto& op = m_operations[cmd.operationKey];
			op.responseData = response;
			op.completed = true;
			op.completedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
			waiters.swap(op.waiters);
		}
		if (cmd.callback) { cmd.callback(response); }
		for (const auto& waiter : waiters)
		{
			if (waiter) { waiter(response); }
		}
	};
	DomainError error;
	auto* service = AiControlService::instance();
	const auto current = static_cast<Permission>(
		static_cast<uint32_t>(cmd.permissions) & static_cast<uint32_t>(service->grantedPermissions()));
	error = checkPermissions(cmd.action, current);
	if (!error.hasError() && QDateTime::currentMSecsSinceEpoch() > cmd.deadlineEpochMs)
	{
		error = {ErrorCode::Timeout, "Deadline expired before execution."};
	}
	if (!error.hasError() && cmd.connectionAlive && !cmd.connectionAlive())
	{
		error = {ErrorCode::Cancelled, "Connection closed before execution."};
	}
	if (!error.hasError() && cmd.action != "get_status"
		&& (!service->isReady() || !service->isRunning() || !Engine::getSong()
			|| Engine::getSong()->isLoadingProject()))
	{
		error = {ErrorCode::NotReady, "Project is not ready."};
	}
	if (error.hasError())
	{
		finish({{"ok", false}, {"error", error.toJson()}});
		return;
	}
	QJsonObject res;

	auto facade = AiApplicationFacade::instance();

	try
	{
		if (cmd.action == "get_status")
		{
			res = facade->getStatus();
			res["permissions"] = QJsonArray::fromStringList(permissionsToStringList(current));
		}
		else if (cmd.action == "get_project") { res = facade->getProject(cmd.payload["projectId"].toString()); }
		else if (cmd.action == "list_tracks")
		{
			res = facade->listTracks(cmd.payload["container"].toString(), cmd.payload["cursor"].toString(),
				cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
		}
		else if (cmd.action == "list_clips")
		{
			res = facade->listClips(cmd.payload["trackId"].toString(),
				static_cast<tick_t>(cmd.payload["startTick"].toVariant().toLongLong()),
				static_cast<tick_t>(cmd.payload["endTick"].toVariant().toLongLong()), cmd.payload["cursor"].toString(),
				cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
		}
		else if (cmd.action == "get_clip_summary") { res = facade->getClipSummary(cmd.payload["clipId"].toString()); }
		else if (cmd.action == "get_automation")
		{
			res = facade->getAutomation(
				cmd.payload["clipId"].toString(), cmd.payload["cursor"].toString(), cmd.payload["limit"].toInt(100));
		}
		else if (cmd.action == "get_notes")
		{
			res = facade->getNotes(cmd.payload["clipId"].toString(),
				static_cast<tick_t>(cmd.payload["startTick"].toVariant().toLongLong()),
				static_cast<tick_t>(cmd.payload["endTick"].toVariant().toLongLong()), cmd.payload["cursor"].toString(),
				cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
		}
		else if (cmd.action == "list_parameters")
		{
			res = facade->listParameters(cmd.payload["ownerId"].toString(), cmd.payload["cursor"].toString(),
				cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
		}
		else if (cmd.action == "list_instruments")
		{
			res = facade->listInstruments(cmd.payload["type"].toString(), cmd.payload["cursor"].toString(),
				cmd.payload["limit"].toInt(DEFAULT_PAGE_LIMIT));
		}
		else if (cmd.action == "get_mixer") { res = facade->getMixer(cmd.payload["channelIndex"].toInt(-1)); }
		else if (cmd.action == "set_transport")
		{
			res = facade->setTransport(cmd.payload["action"].toString(),
				static_cast<tick_t>(cmd.payload["tickPosition"].toVariant().toLongLong()));
		}
		else if (cmd.action == "preview_changes")
		{
			res = facade->previewChanges(cmd.payload["projectId"].toString(),
				cmd.payload["expectedRevision"].toVariant().toULongLong(), cmd.payload["changes"].toArray());
		}
		else if (cmd.action == "apply_changes")
		{
			res = facade->applyChanges(cmd.payload["projectId"].toString(),
				cmd.payload["expectedRevision"].toVariant().toULongLong(), cmd.payload["planId"].toString(),
				cmd.operationKey, cmd.deadlineEpochMs);
		}
		else if (cmd.action == "undo")
		{
			res = facade->undo(
				cmd.payload["expectedRevision"].toVariant().toULongLong(), cmd.payload["undoGroupId"].toString());
		}
		else if (cmd.action == "redo")
		{
			res = facade->redo(
				cmd.payload["expectedRevision"].toVariant().toULongLong(), cmd.payload["undoGroupId"].toString());
		}
		else if (cmd.action == "create_instrument_track")
		{
			res = facade->createInstrumentTrack(
				cmd.payload["pluginKey"].toString(), cmd.payload["trackName"].toString());
		}
		else if (cmd.action == "list_assets")
		{
			res = facade->listAssets(
				cmd.payload["rootId"].toString(), cmd.payload["cursor"].toString(), cmd.payload["limit"].toInt(100));
		}
		else if (cmd.action == "load_asset")
		{
			res = facade->loadAsset(cmd.payload["assetId"].toString(), cmd.payload["targetTrackId"].toString());
		}
		else if (cmd.action == "save_project_copy")
		{
			res = facade->saveProjectCopy(cmd.payload["fileName"].toString());
		}
		else if (cmd.action == "start_render")
		{
			res = facade->startRender(cmd.payload["outputFileName"].toString(), cmd.payload["format"].toString("wav"),
				cmd.payload["sampleRate"].toInt(44100), cmd.payload["bitDepth"].toInt(16));
		}
		else if (cmd.action == "get_job") { res = facade->getJob(cmd.payload["jobId"].toString()); }
		else if (cmd.action == "cancel_job") { res = facade->cancelJob(cmd.payload["jobId"].toString()); }
		else if (cmd.action == "read_artifact")
		{
			res = AiJobManager::instance()->readArtifact(cmd.payload["artifactId"].toString(),
				cmd.payload["offset"].toVariant().toLongLong(), cmd.payload["limit"].toInt(256 * 1024));
		}
		else if (cmd.action == "get_operation")
		{
			QJsonObject opRes;
			QString originalAction;
			{
				QMutexLocker lock(&m_mutex);
				originalAction = m_operations.value(cmd.payload["operationKey"].toString()).action;
			}
			const auto lookupPermission = checkPermissions(originalAction, current);
			if (lookupPermission.hasError()) { res = {{"ok", false}, {"error", lookupPermission.toJson()}}; }
			else if (lookupOperation(cmd.payload["operationKey"].toString(), opRes))
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
			res["error"]
				= DomainError{ErrorCode::UnsupportedOperation, QString("Unknown action: %1").arg(cmd.action)}.toJson();
		}
	}
	catch (const std::exception&)
	{
		res = {{"ok", false}, {"error", DomainError{ErrorCode::InternalError, "Domain operation failed."}.toJson()}};
	}
	catch (...)
	{
		res = {{"ok", false},
			{"error", DomainError{ErrorCode::InternalError, "Unexpected domain operation failure."}.toJson()}};
	}
	finish(res);
}

} // namespace lmms::ai
