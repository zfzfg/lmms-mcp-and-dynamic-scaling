#include "AiCommandDispatcher.h"

#include <atomic>
#include <chrono>
#include <memory>

#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <exception>

#include "ThreadPool.h"

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
const QStringList& mutationActions()
{
	static const QStringList actions{"apply_changes", "create_instrument_track", "load_asset", "set_transport", "undo",
		"redo", "start_render", "save_project_copy", "cancel_job"};
	return actions;
}

bool isMutationAction(const QString& action)
{
	return mutationActions().contains(action);
}

QJsonObject shutdownResponse()
{
	return {{"ok", false}, {"error", DomainError{ErrorCode::Cancelled, "Service restarted."}.toJson()}};
}

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
	{
		QMutexLocker lock(&m_mutex);
		if (m_shuttingDown)
		{
			lock.unlock();
			reject({ErrorCode::Cancelled, "Service restarted."});
			return;
		}
	}
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
	const QStringList& mutations = mutationActions();
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
	if (m_shuttingDown)
	{
		lock.unlock();
		reject({ErrorCode::Cancelled, "Service restarted."});
		return;
	}
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
	// Completed keys are replayable for 24 hours. Never drop a younger key, and
	// never drop a key that is still incomplete. Large non-mutation bodies may
	// be compacted; mutation responses stay intact for idempotent replay.
	for (auto it = m_operations.begin(); it != m_operations.end();)
	{
		if (it->completed && now - it->completedAtEpochMs > 24LL * 3600 * 1000) { it = m_operations.erase(it); }
		else { ++it; }
	}
	if (!operationKey.isEmpty())
	{
		if (m_operations.size() >= MAX_OPERATION_HISTORY)
		{
			trimBulkyCompletedResponsesLocked();
			if (m_operations.size() >= MAX_OPERATION_HISTORY)
			{
				const int retained = m_operations.size();
				lock.unlock();
				reject({ErrorCode::Busy,
					QString("Operation register is full: %1 young keys are retained for 24 hours. Waiting for those "
							"keys to expire frees a slot.")
						.arg(retained)});
				return;
			}
		}
		if (m_shuttingDown)
		{
			lock.unlock();
			reject({ErrorCode::Cancelled, "Service restarted."});
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

void AiCommandDispatcher::takePendingCallbacksLocked(QList<std::function<void(const QJsonObject&)>>& callbacks)
{
	while (!m_queue.isEmpty())
	{
		QueuedCommand command = m_queue.dequeue();
		if (command.callback) { callbacks.append(std::move(command.callback)); }
	}
	const QJsonObject cancelled = shutdownResponse();
	const auto now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = m_operations.begin(); it != m_operations.end(); ++it)
	{
		if (it->completed) { continue; }
		for (auto& waiter : it->waiters)
		{
			if (waiter) { callbacks.append(std::move(waiter)); }
		}
		it->waiters.clear();
		it->completed = true;
		it->completedAtEpochMs = now;
		it->responseData = cancelled;
	}
	if (m_inFlightCallbackPending && m_inFlightCallback)
	{
		callbacks.append(std::move(m_inFlightCallback));
		m_inFlightCallback = nullptr;
		m_inFlightCallbackPending = false;
	}
	m_asyncBusy = false;
	m_asyncCommand = {};
}

void AiCommandDispatcher::trimBulkyCompletedResponsesLocked()
{
	for (auto it = m_operations.begin(); it != m_operations.end(); ++it)
	{
		if (!it->completed || isMutationAction(it->action)) { continue; }
		it->responseData = retainedResponse(it->action, it->responseData);
	}
}

QJsonObject AiCommandDispatcher::retainedResponse(const QString& action, const QJsonObject& response) const
{
	// Mutation replay must observe the original result. Reads may keep only
	// ok/error identity once the stored JSON exceeds 64 KiB.
	if (isMutationAction(action)) { return response; }
	if (QJsonDocument(response).toJson(QJsonDocument::Compact).size() <= MAX_RETAINED_RESPONSE_BYTES)
	{
		return response;
	}
	QJsonObject stub;
	stub["ok"] = response.value("ok");
	if (response.contains("error")) { stub["error"] = response.value("error"); }
	stub["message"] = "Bulky payload was trimmed.";
	return stub;
}

void AiCommandDispatcher::reset()
{
	QList<std::function<void(const QJsonObject&)>> callbacks;
	{
		QMutexLocker lock(&m_mutex);
		m_shuttingDown = false;
		takePendingCallbacksLocked(callbacks);
		m_operations.clear();
		m_operationHistory.clear();
		m_isProcessing = false;
	}
	const QJsonObject cancelled = shutdownResponse();
	for (const auto& callback : callbacks)
	{
		if (callback) { callback(cancelled); }
	}
}

void AiCommandDispatcher::beginShutdown()
{
	QList<std::function<void(const QJsonObject&)>> callbacks;
	{
		QMutexLocker lock(&m_mutex);
		m_shuttingDown = true;
		takePendingCallbacksLocked(callbacks);
	}
	const QJsonObject cancelled = shutdownResponse();
	for (const auto& callback : callbacks)
	{
		if (callback) { callback(cancelled); }
	}
}

bool AiCommandDispatcher::lookupOperation(const QString& opKey, QJsonObject& outResult) const
{
	QMutexLocker locker(&m_mutex);
	if (!m_operations.contains(opKey)) { return false; }
	outResult = m_operations.value(opKey).responseData;
	return true;
}

void AiCommandDispatcher::finishCommand(const QueuedCommand& cmd, const QJsonObject& response)
{
	AiControlService::instance()->recordResult(cmd.action, response);
	QList<std::function<void(const QJsonObject&)>> waiters;
	bool deliverInFlight = false;
	std::function<void(const QJsonObject&)> callback;
	{
		QMutexLocker lock(&m_mutex);
		if (!cmd.operationKey.isEmpty() && m_operations.contains(cmd.operationKey))
		{
			auto& op = m_operations[cmd.operationKey];
			if (!op.completed)
			{
				op.responseData = retainedResponse(cmd.action, response);
				op.completed = true;
				op.completedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
				waiters.swap(op.waiters);
			}
		}
		if (m_inFlightCallbackPending)
		{
			deliverInFlight = true;
			m_inFlightCallbackPending = false;
			callback = std::move(m_inFlightCallback);
			m_inFlightCallback = nullptr;
		}
	}
	if (deliverInFlight && callback) { callback(response); }
	for (const auto& waiter : waiters)
	{
		if (waiter) { waiter(response); }
	}
}

void AiCommandDispatcher::completeAsyncRead(const QJsonObject& result)
{
	QueuedCommand cmd;
	{
		QMutexLocker lock(&m_mutex);
		if (!m_asyncBusy) { return; }
		m_asyncBusy = false;
		cmd = std::move(m_asyncCommand);
		m_asyncCommand = {};
	}
	finishCommand(cmd, result);
	emit commandEnqueued();
}

void AiCommandDispatcher::processNextCommand()
{
	QueuedCommand cmd;
	bool cancelled = false;
	{
		QMutexLocker locker(&m_mutex);
		if (m_asyncBusy || m_queue.isEmpty()) { return; }
		cmd = m_queue.dequeue();
		m_inFlightCallbackPending = static_cast<bool>(cmd.callback);
		m_inFlightCallback = cmd.callback;
		cancelled = m_shuttingDown;
	}

	auto finish = [&](const QJsonObject& response) { finishCommand(cmd, response); };
	if (cancelled)
	{
		finish(shutdownResponse());
		return;
	}
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
	{
		QMutexLocker locker(&m_mutex);
		cancelled = m_shuttingDown;
	}
	if (cancelled)
	{
		finish(shutdownResponse());
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
			const QString artifactId = cmd.payload["artifactId"].toString();
			const qint64 offset = cmd.payload["offset"].toVariant().toLongLong();
			const int limit = cmd.payload["limit"].toInt(256 * 1024);
			{
				QMutexLocker lock(&m_mutex);
				m_asyncBusy = true;
				m_asyncCommand = cmd;
			}
			auto accepted = std::make_shared<std::atomic<bool>>(false);
			QPointer<AiCommandDispatcher> self(this);
			auto future = ThreadPool::instance().enqueue([self, artifactId, offset, limit, accepted] {
				accepted->store(true, std::memory_order_release);
				QJsonObject result;
				try
				{
					result = AiJobManager::instance()->readArtifact(artifactId, offset, limit);
				}
				catch (const std::exception&)
				{
					result = {{"ok", false},
						{"error", DomainError{ErrorCode::InternalError, "Artifact read failed."}.toJson()}};
				}
				catch (...)
				{
					result = {{"ok", false},
						{"error", DomainError{ErrorCode::InternalError, "Artifact read failed."}.toJson()}};
				}
				if (!self) { return; }
				QMetaObject::invokeMethod(
					self.data(), [self, result] {
						if (self) { self->completeAsyncRead(result); }
					},
					Qt::QueuedConnection);
			});
			const bool rejected = future.wait_for(std::chrono::seconds(0)) == std::future_status::ready
				&& !accepted->load(std::memory_order_acquire);
			if (rejected)
			{
				{
					QMutexLocker lock(&m_mutex);
					m_asyncBusy = false;
					m_asyncCommand = {};
				}
				finish({{"ok", false},
					{"error", DomainError{ErrorCode::Busy, "Artifact read could not be started."}.toJson()}});
			}
			return;
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
