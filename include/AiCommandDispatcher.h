#ifndef LMMS_AI_COMMAND_DISPATCHER_H
#define LMMS_AI_COMMAND_DISPATCHER_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <functional>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms::ai {

struct QueuedCommand
{
	qint64 requestId{0};
	QString action;
	QJsonObject payload;
	QString operationKey;
	qint64 deadlineEpochMs{0};
	Permission permissions{Permission::None};
	std::function<bool()> connectionAlive;
	std::function<void(const QJsonObject& response)> callback;
};

struct OperationRecord
{
	QString operationKey;
	QString action;
	QJsonObject requestPayload;
	QJsonObject responseData;
	qint64 completedAtEpochMs{0};
	bool completed{false};
	QList<std::function<void(const QJsonObject&)>> waiters;
};

class LMMS_EXPORT AiCommandDispatcher : public QObject
{
	Q_OBJECT
public:
	static AiCommandDispatcher* instance();

	// Enqueue a command from any thread (e.g. IPC thread)
	void dispatch(qint64 requestId, const QString& action, const QJsonObject& payload, const QString& operationKey,
		int deadlineMs, Permission grantedPermissions, std::function<void(const QJsonObject& response)> callback,
		std::function<bool()> connectionAlive = {});

	void reset();
	// Reject new commands and complete every queued callback and in-flight waiter once.
	void beginShutdown();
	// Operation registry lookup
	bool lookupOperation(const QString& opKey, QJsonObject& outResult) const;

signals:
	void commandEnqueued();

private slots:
	void processNextCommand();

private:
	AiCommandDispatcher();
	~AiCommandDispatcher() override = default;

	void takePendingCallbacksLocked(QList<std::function<void(const QJsonObject&)>>& callbacks);
	void trimBulkyCompletedResponsesLocked();
	QJsonObject retainedResponse(const QString& action, const QJsonObject& response) const;
	void finishCommand(const QueuedCommand& cmd, const QJsonObject& response);
	void completeAsyncRead(const QJsonObject& result);

	mutable QMutex m_mutex;
	QQueue<QueuedCommand> m_queue;
	bool m_isProcessing{false};
	bool m_shuttingDown{false};
	bool m_asyncBusy{false};
	QueuedCommand m_asyncCommand;
	bool m_inFlightCallbackPending{false};
	std::function<void(const QJsonObject&)> m_inFlightCallback;

	// Idempotency register. Completed keys stay replayable for 24 hours and are
	// never removed early. Incomplete keys are never evicted. A completed
	// non-mutation response larger than 64 KiB may be replaced with a compact
	// stub that keeps the same ok/error identity and says the bulky payload was
	// trimmed. Mutation responses are kept whole so replay stays identical.
	// When every slot is still a young key, new keyed operations return Busy.
	QHash<QString, OperationRecord> m_operations;
	QList<QString> m_operationHistory;
	static constexpr int MAX_OPERATION_HISTORY = 1024;
	static constexpr int MAX_RETAINED_RESPONSE_BYTES = 64 * 1024;
};

} // namespace lmms::ai

#endif // LMMS_AI_COMMAND_DISPATCHER_H
