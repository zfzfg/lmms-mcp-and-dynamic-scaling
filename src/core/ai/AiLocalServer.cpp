#include "AiLocalServer.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QtEndian>
#include <cmath>

#include "AiCommandDispatcher.h"
#include "AiControlService.h"

namespace lmms::ai {

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
	// The random endpoint belongs exclusively to this service.
	m_server->setSocketOptions(QLocalServer::UserAccessOption);
	return m_server->listen(pipeOrSocketName);
}

void AiLocalServer::stopListening()
{
	if (m_server && m_server->isListening()) { m_server->close(); }

	revokeSessions();
}

void AiLocalServer::revokeSessions()
{
	QMutexLocker locker(&m_mutex);
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
	{
		QLocalSocket* sock = it.key();
		*it->alive = false;
		sock->disconnect(this);
		sock->close();
		sock->deleteLater();
	}
	m_clients.clear();
	m_writer.clear();
}

bool AiLocalServer::isListening() const
{
	return m_server && m_server->isListening();
}

QString AiLocalServer::serverName() const
{
	return m_server ? m_server->fullServerName() : QString();
}

void AiLocalServer::onNewConnection()
{
	while (m_server->hasPendingConnections())
	{
		QLocalSocket* socket = m_server->nextPendingConnection();
		if (!socket) { continue; }
		if (m_clients.size() >= 8)
		{
			socket->abort();
			socket->deleteLater();
			continue;
		}
		socket->setReadBufferSize(MAX_IPC_FRAME_SIZE + 4);
		QTimer::singleShot(3000, socket, [this, socket] {
			if (m_clients.contains(socket) && !m_clients.value(socket).authenticated) { socket->abort(); }
		});

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
	auto* socket = qobject_cast<QLocalSocket*>(sender());
	if (!socket) { return; }
	while (m_clients.contains(socket) && socket->state() == QLocalSocket::ConnectedState)
	{
		auto& buffer = m_clients[socket].readBuffer;
		buffer.append(socket->read(qMax<qint64>(0, MAX_IPC_FRAME_SIZE + 4 - buffer.size())));
		if (buffer.size() < 4) { return; }
		quint32 length = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buffer.constData()));
		if (!length || length > MAX_IPC_FRAME_SIZE)
		{
			socket->abort();
			return;
		}
		if (buffer.size() < static_cast<int>(length + 4)) { return; }
		const auto frame = buffer.mid(4, length);
		buffer.remove(0, length + 4);
		processFrame(socket, frame);
		// processFrame can remove the session; do not retain references across it.
	}
}

void AiLocalServer::onSocketDisconnected()
{
	QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
	if (!socket) { return; }

	QMutexLocker locker(&m_mutex);
	if (m_clients.contains(socket)) { *m_clients[socket].alive = false; }
	if (m_writer == socket) { m_writer.clear(); }
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
	const auto requestId = req["id"];
	if (req["jsonrpc"].toString() != "2.0" || !requestId.isDouble() || requestId.toDouble() < 0
		|| requestId.toDouble() > 9007199254740991.0 || requestId.toDouble() != std::floor(requestId.toDouble())
		|| !req["params"].isObject())
	{
		sendResponse(socket, 0, {}, {ErrorCode::ValidationError, "Invalid IPC envelope."});
		return;
	}
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

		if (instId == AiControlService::instance()->instanceId() && token == AiControlService::instance()->authToken())
		{
			if (params["ipcProtocolVersion"].toString() != "1.0")
			{
				sendResponse(socket, id, {}, {ErrorCode::UnsupportedOperation, "Unsupported IPC version."});
				return;
			}
			Permission granted = AiControlService::instance()->grantedPermissions();
			if (params.contains("requestedPermissions"))
			{
				QStringList requested;
				for (const auto& value : params["requestedPermissions"].toArray())
				{
					requested << value.toString();
				}
				granted = static_cast<Permission>(
					static_cast<uint32_t>(granted) & static_cast<uint32_t>(permissionsFromStringList(requested)));
			}
			if (hasPermission(granted, Permission::WriteProject))
			{
				if (m_writer && m_writer != socket)
				{
					sendResponse(socket, id, {}, {ErrorCode::WriterBusy, "Another writer is connected."});
					return;
				}
				m_writer = socket;
			}
			{
				QMutexLocker locker(&m_mutex);
				if (m_clients.contains(socket))
				{
					m_clients[socket].authenticated = true;
					m_clients[socket].grantedPermissions = granted;
					m_clients[socket].permissionGeneration = AiControlService::instance()->permissionGeneration();
				}
			}

			QJsonObject res;
			res["ok"] = true;
			res["instanceId"] = instId;
			res["bridgeVersion"] = "1.3.0-alpha";
			res["ipcProtocolVersion"] = "1.0";
			res["permissions"] = QJsonArray::fromStringList(permissionsToStringList(granted));
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
		if (!params["payload"].isObject()
			|| (params.contains("deadlineMs")
				&& (!params["deadlineMs"].isDouble() || params["deadlineMs"].toDouble() != deadlineMs || deadlineMs < 1
					|| deadlineMs > 30000)))
		{
			sendResponse(socket, id, {}, {ErrorCode::ValidationError, "Invalid payload or deadline."});
			return;
		}

		if (m_clients[socket].pending >= 64)
		{
			sendResponse(socket, id, {}, {ErrorCode::Busy, "Session request limit reached."});
			return;
		}
		++m_clients[socket].pending;
		const auto receivedAt = QDateTime::currentMSecsSinceEpoch();
		const auto generation = m_clients.value(socket).permissionGeneration;
		auto alive = m_clients.value(socket).alive;
		QPointer<QLocalSocket> guardedSocket(socket);
		QPointer<AiLocalServer> server(this);
		auto* dispatcher = AiCommandDispatcher::instance();
		QMetaObject::invokeMethod(
			dispatcher,
			[dispatcher, id, action, payload, opKey, deadlineMs, perms, alive, server, guardedSocket, receivedAt,
				generation] {
				dispatcher->dispatch(
					id, action, payload, opKey,
					static_cast<int>(deadlineMs - (QDateTime::currentMSecsSinceEpoch() - receivedAt)), perms,
					[server, guardedSocket, id](const QJsonObject& result) {
						if (!server) { return; }
						QMetaObject::invokeMethod(
							server,
							[server, guardedSocket, id, result] {
								if (server && guardedSocket)
								{
									if (server->m_clients.contains(guardedSocket))
									{
										--server->m_clients[guardedSocket].pending;
									}
									server->sendResponse(guardedSocket, id, result);
								}
							},
							Qt::QueuedConnection);
					},
					[alive, generation] {
						return alive->load() && generation == AiControlService::instance()->permissionGeneration();
					});
			},
			Qt::QueuedConnection);
	}
	else
	{
		DomainError err{ErrorCode::UnsupportedOperation, QString("Unknown method: %1").arg(method)};
		sendResponse(socket, id, QJsonObject(), err);
	}
}

void AiLocalServer::sendFrame(QLocalSocket* socket, const QByteArray& frame)
{
	if (!socket || socket->state() != QLocalSocket::ConnectedState) { return; }

	if (frame.size() > static_cast<int>(MAX_IPC_FRAME_SIZE) || socket->bytesToWrite() > MAX_IPC_FRAME_SIZE * 2)
	{
		socket->abort();
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

	if (error.hasError()) { envelope["error"] = error.toJson(); }
	else { envelope["result"] = result; }

	QByteArray rawJson = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
	sendFrame(socket, rawJson);
}

} // namespace lmms::ai
