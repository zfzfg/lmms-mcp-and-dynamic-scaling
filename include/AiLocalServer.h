#ifndef LMMS_AI_LOCAL_SERVER_H
#define LMMS_AI_LOCAL_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms::ai {

struct ClientSession
{
	bool authenticated{false};
	int pending{0};
	uint64_t permissionGeneration{0};
	std::shared_ptr<std::atomic_bool> alive{std::make_shared<std::atomic_bool>(true)};
	Permission grantedPermissions{Permission::None};
	QByteArray readBuffer;
};

class LMMS_EXPORT AiLocalServer : public QObject
{
	Q_OBJECT
public:
	explicit AiLocalServer(QObject* parent = nullptr);
	~AiLocalServer() override;

	bool startListening(const QString& pipeOrSocketName);
	void stopListening();
	void revokeSessions();
	bool isListening() const;
	QString serverName() const;

private slots:
	void onNewConnection();
	void onReadyRead();
	void onSocketDisconnected();

private:
	void processFrame(QLocalSocket* socket, const QByteArray& frame);
	void sendFrame(QLocalSocket* socket, const QByteArray& frame);
	void sendResponse(
		QLocalSocket* socket, qint64 id, const QJsonObject& result, const DomainError& error = DomainError());

	QPointer<QLocalSocket> m_writer;
	QLocalServer* m_server{nullptr};
	mutable QMutex m_mutex;
	QHash<QLocalSocket*, ClientSession> m_clients;
};

} // namespace lmms::ai

#endif // LMMS_AI_LOCAL_SERVER_H
