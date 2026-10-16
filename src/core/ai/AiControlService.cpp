#include "AiControlService.h"
#include "AiLocalServer.h"
#include "AiObjectRegistry.h"
#include "AiProjectRevision.h"
#include "AiParameterRegistry.h"
#include "Engine.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace lmms::ai
{

AiControlService* AiControlService::instance()
{
	static AiControlService s_instance;
	return &s_instance;
}

AiControlService::AiControlService()
	: QObject(nullptr)
{
}

AiControlService::~AiControlService()
{
	stopService();
}

bool AiControlService::startService(const QString& customConnectionFilePath)
{
	if (m_running)
	{
		return true;
	}

	m_instanceId = QString("inst_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	m_authToken = QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_endpointName = QString("lmms-mcp-%1").arg(m_instanceId);

	if (!customConnectionFilePath.isEmpty())
	{
		m_connectionFilePath = customConnectionFilePath;
	}
	else
	{
		QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		m_connectionFilePath = QDir::cleanPath(baseDir + "/mcp/connections/studio.json");
	}

	m_server = std::make_unique<AiLocalServer>(this);
	if (!m_server->startListening(m_endpointName))
	{
		m_server.reset();
		return false;
	}

	writeConnectionFile();
	m_running = true;

	if (Engine::getSong())
	{
		AiParameterRegistry::instance()->registerBuiltins(Engine::getSong());
		AiProjectRevision::instance()->attachToSong(Engine::getSong());
	}

	return true;
}

void AiControlService::stopService()
{
	if (!m_running)
	{
		return;
	}

	m_ready = false;
	m_running = false;

	if (m_server)
	{
		m_server->stopListening();
		m_server.reset();
	}

	removeConnectionFile();
	AiObjectRegistry::instance()->invalidateAll();
}

void AiControlService::setReady(bool ready)
{
	m_ready = ready;
}

void AiControlService::writeConnectionFile()
{
	if (m_connectionFilePath.isEmpty()) { return; }

	QFileInfo fi(m_connectionFilePath);
	QDir dir = fi.dir();
	if (!dir.exists())
	{
		dir.mkpath(".");
	}

	QJsonObject obj;
	obj["instanceId"] = m_instanceId;
	obj["endpoint"] = m_endpointName;
	obj["authToken"] = m_authToken;
	obj["bridgeVersion"] = "1.3.0-alpha";
	obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	QFile file(m_connectionFilePath);
	if (file.open(QIODevice::WriteOnly))
	{
		file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
		file.close();
		// Set file permissions to user read/write only
		file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
	}
}

void AiControlService::removeConnectionFile()
{
	if (!m_connectionFilePath.isEmpty() && QFile::exists(m_connectionFilePath))
	{
		QFile::remove(m_connectionFilePath);
	}
}

} // namespace lmms::ai
