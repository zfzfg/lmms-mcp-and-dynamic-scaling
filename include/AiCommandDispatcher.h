#ifndef LMMS_AI_COMMAND_DISPATCHER_H
#define LMMS_AI_COMMAND_DISPATCHER_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
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
	// Operation registry lookup
	bool lookupOperation(const QString& opKey, QJsonObject& outResult) const;

signals:
	void commandEnqueued();

private slots:
	void processNextCommand();

private:
	AiCommandDispatcher();
	~AiCommandDispatcher() override = default;

	mutable QMutex m_mutex;
	QQueue<QueuedCommand> m_queue;
	bool m_isProcessing{false};

	// Key deduplication register (bounded up to 1024)
	QHash<QString, OperationRecord> m_operations;
	QList<QString> m_operationHistory;
	static constexpr int MAX_OPERATION_HISTORY = 1024;
};

} // namespace lmms::ai

#endif // LMMS_AI_COMMAND_DISPATCHER_H
