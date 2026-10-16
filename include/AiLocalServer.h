#ifndef LMMS_AI_LOCAL_SERVER_H
#define LMMS_AI_LOCAL_SERVER_H

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include "lmms_export.h"
#include "AiDomainTypes.h"

namespace lmms::ai
{

struct ClientSession
{
	bool authenticated{false};
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
	bool isListening() const;
	QString serverName() const;

private slots:
	void onNewConnection();
	void onReadyRead();
	void onSocketDisconnected();

private:
	void processFrame(QLocalSocket* socket, const QByteArray& frame);
	void sendFrame(QLocalSocket* socket, const QByteArray& frame);
	void sendResponse(QLocalSocket* socket, qint64 id, const QJsonObject& result, const DomainError& error = DomainError());

	QLocalServer* m_server{nullptr};
	mutable QMutex m_mutex;
	QHash<QLocalSocket*, ClientSession> m_clients;
};

} // namespace lmms::ai

#endif // LMMS_AI_LOCAL_SERVER_H
