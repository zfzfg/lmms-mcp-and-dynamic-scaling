#include "AiLocalServer.h"
#include "AiControlService.h"
#include "AiCommandDispatcher.h"
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>

namespace lmms::ai
{

AiLocalServer::AiLocalServer(QObject* parent)
	: QObject(parent)
{
	m_server = new QLocalServer(this);
	connect(m_server, &QLocalServer::newConnection, this, &AiLocalServer::onNewConnection);
}

AiLocalServer::~AiLocalServer()
{
	stopListening();
}

bool AiLocalServer::startListening(const QString& pipeOrSocketName)
{
	stopListening();
	QLocalServer::removeServer(pipeOrSocketName);
	m_server->setSocketOptions(QLocalServer::UserAccessOption);
	return m_server->listen(pipeOrSocketName);
}

void AiLocalServer::stopListening()
{
	if (m_server && m_server->isListening())
	{
		m_server->close();
	}

	QMutexLocker locker(&m_mutex);
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
	{
		QLocalSocket* sock = it.key();
		sock->disconnect(this);
		sock->close();
		sock->deleteLater();
	}
	m_clients.clear();
}

bool AiLocalServer::isListening() const
{
	return m_server && m_server->isListening();
}

QString AiLocalServer::serverName() const
{
	return m_server ? m_server->serverName() : QString();
}

void AiLocalServer::onNewConnection()
{
	while (m_server->hasPendingConnections())
	{
		QLocalSocket* socket = m_server->nextPendingConnection();
		if (!socket) { continue; }

		{
			QMutexLocker locker(&m_mutex);
			ClientSession session;
			m_clients.insert(socket, session);
		}

		connect(socket, &QLocalSocket::readyRead, this, &AiLocalServer::onReadyRead);
		connect(socket, &QLocalSocket::disconnected, this, &AiLocalServer::onSocketDisconnected);
	}
}

void AiLocalServer::onReadyRead()
{
	QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
	if (!socket) { return; }

	QMutexLocker locker(&m_mutex);
	if (!m_clients.contains(socket)) { return; }

	ClientSession& session = m_clients[socket];
	session.readBuffer.append(socket->readAll());

	while (session.readBuffer.size() >= 4)
	{
		quint32 frameLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(session.readBuffer.constData()));

		if (frameLen > MAX_IPC_FRAME_SIZE)
		{
			// Frame size violation - close connection
			socket->close();
			return;
		}

		if (static_cast<quint32>(session.readBuffer.size()) < 4 + frameLen)
		{
			// Incomplete frame, wait for more data
			break;
		}

		QByteArray frameData = session.readBuffer.mid(4, frameLen);
		session.readBuffer.remove(0, 4 + frameLen);

		// Unlock mutex before processing frame to prevent deadlocks
		locker.unlock();
		processFrame(socket, frameData);
		locker.relock();
	}
}

void AiLocalServer::onSocketDisconnected()
{
	QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
	if (!socket) { return; }

	QMutexLocker locker(&m_mutex);
	m_clients.remove(socket);
	socket->deleteLater();
}

void AiLocalServer::processFrame(QLocalSocket* socket, const QByteArray& frame)
{
	QJsonParseError parseErr;
	QJsonDocument doc = QJsonDocument::fromJson(frame, &parseErr);
	if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
	{
		DomainError err{ErrorCode::ValidationError, "Malformed JSON frame received."};
		sendResponse(socket, 0, QJsonObject(), err);
		return;
	}

	QJsonObject req = doc.object();
	qint64 id = req["id"].toVariant().toLongLong();
	QString method = req["method"].toString();
	QJsonObject params = req["params"].toObject();

	bool isAuth = false;
	Permission perms = Permission::None;
	{
		QMutexLocker locker(&m_mutex);
		if (m_clients.contains(socket))
		{
			isAuth = m_clients[socket].authenticated;
			perms = m_clients[socket].grantedPermissions;
		}
	}

	if (method == "auth.handshake")
	{
		QString instId = params["instanceId"].toString();
		QString token = params["authToken"].toString();

		if (instId == AiControlService::instance()->instanceId() &&
		    token == AiControlService::instance()->authToken())
		{
			Permission granted = AiControlService::instance()->grantedPermissions();
			{
				QMutexLocker locker(&m_mutex);
				if (m_clients.contains(socket))
				{
					m_clients[socket].authenticated = true;
					m_clients[socket].grantedPermissions = granted;
				}
			}

			QJsonObject res;
			res["ok"] = true;
			res["instanceId"] = instId;
			res["bridgeVersion"] = "1.3.0-alpha";
			res["permissions"] = permissionsToStringList(granted);
			res["ready"] = AiControlService::instance()->isReady();

			sendResponse(socket, id, res);
		}
		else
		{
			DomainError err{ErrorCode::PermissionDenied, "Invalid instance ID or authentication token."};
			sendResponse(socket, id, QJsonObject(), err);
			socket->close();
		}
		return;
	}

	if (!isAuth)
	{
		DomainError err{ErrorCode::PermissionDenied, "Connection must perform auth.handshake before issuing calls."};
		sendResponse(socket, id, QJsonObject(), err);
		return;
	}

	if (method == "domain.call")
	{
		QString action = params["action"].toString();
		QJsonObject payload = params["payload"].toObject();
		QString opKey = params["operationKey"].toString();
		int deadlineMs = params["deadlineMs"].toInt(DEFAULT_DEADLINE_MS);

		AiCommandDispatcher::instance()->dispatch(id, action, payload, opKey, deadlineMs, perms,
			[this, socket, id](const QJsonObject& res)
			{
				QJsonObject envelope;
				envelope["jsonrpc"] = "2.0";
				envelope["id"] = id;
				envelope["result"] = res;
				QByteArray rawJson = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
				sendFrame(socket, rawJson);
			});
	}
	else
	{
		DomainError err{ErrorCode::UnsupportedOperation, QString("Unknown method: %1").arg(method)};
		sendResponse(socket, id, QJsonObject(), err);
	}
}

void AiLocalServer::sendFrame(QLocalSocket* socket, const QByteArray& frame)
{
	if (!socket || socket->state() != QLocalSocket::ConnectedState)
	{
		return;
	}

	quint32 len = static_cast<quint32>(frame.size());
	quint32 bigEndianLen = qToBigEndian(len);

	socket->write(reinterpret_cast<const char*>(&bigEndianLen), 4);
	socket->write(frame);
	socket->flush();
}

void AiLocalServer::sendResponse(QLocalSocket* socket, qint64 id, const QJsonObject& result, const DomainError& error)
{
	QJsonObject envelope;
	envelope["jsonrpc"] = "2.0";
	envelope["id"] = id;

	if (error.hasError())
	{
		envelope["error"] = error.toJson();
	}
	else
	{
		envelope["result"] = result;
	}

	QByteArray rawJson = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
	sendFrame(socket, rawJson);
}

} // namespace lmms::ai
