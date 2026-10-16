#include "AiControlService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>

#include "AiChangePlan.h"
#include "AiCommandDispatcher.h"
#include "AiFilePolicy.h"
#include "AiJobManager.h"
#include "AiLocalServer.h"
#include "AiObjectRegistry.h"
#include "AiParameterRegistry.h"
#include "AiProjectRevision.h"
#include "ConfigManager.h"
#include "Engine.h"
#ifdef Q_OS_WIN
#include <windows.h>

#include <aclapi.h>
#include <io.h>
#include <vector>
#else
#include <unistd.h>
#endif

namespace lmms::ai {

namespace {
bool restrictConnectionFile(QFileDevice& file)
{
#ifdef Q_OS_WIN
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { return false; }
	DWORD bytes = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
	std::vector<BYTE> buffer(bytes);
	const bool read = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes);
	CloseHandle(token);
	if (!read) { return false; }
	auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
	EXPLICIT_ACCESSW access{};
	access.grfAccessPermissions = FILE_ALL_ACCESS;
	access.grfAccessMode = SET_ACCESS;
	access.grfInheritance = NO_INHERITANCE;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.TrusteeType = TRUSTEE_IS_USER;
	access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(user->User.Sid);
	PACL acl = nullptr;
	if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) { return false; }
	HANDLE reopened = ReOpenFile(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())), WRITE_DAC | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
	const bool secured = reopened != INVALID_HANDLE_VALUE
		&& SetSecurityInfo(reopened, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
			   nullptr, nullptr, acl, nullptr)
			== ERROR_SUCCESS;
	if (reopened != INVALID_HANDLE_VALUE) { CloseHandle(reopened); }
	LocalFree(acl);
	return secured;
#else
	return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
#endif
}
} // namespace

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
	if (m_running) { return true; }

	m_instanceId = QString("inst_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	m_authToken
		= QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_endpointName = QString("lmms-mcp-%1").arg(m_instanceId);

	if (!customConnectionFilePath.isEmpty()) { m_connectionFilePath = customConnectionFilePath; }
	else
	{
		QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		m_connectionFilePath = QDir::cleanPath(baseDir + "/mcp/connections/" + m_instanceId + ".json");
	}

	// Create the dispatcher on the application thread before any IPC request can arrive.
	AiCommandDispatcher::instance()->reset();
	AiFilePolicy::instance()->setAllowedDirectory(
		AiFilePolicy::RootType::Libraries, ConfigManager::inst()->userSamplesDir());
	AiFilePolicy::instance()->setAllowedDirectory(
		AiFilePolicy::RootType::Presets, ConfigManager::inst()->userPresetsDir());
	AiParameterRegistry::instance()->registerBuiltins(Engine::getSong());
	AiProjectRevision::instance()->attachToSong(Engine::getSong());
	m_lastError.clear();
	m_server = std::make_unique<AiLocalServer>();
	m_server->moveToThread(&m_ioThread);
	m_running = true;
	connect(&m_ioThread, &QThread::started, m_server.get(), [this] {
		const bool listening = m_server->startListening(m_endpointName);
		const QString address = m_server->serverName();
		QMetaObject::invokeMethod(
			this,
			[this, listening, address] {
				if (!m_running) { return; }
				if (!listening)
				{
					m_lastError = "Cannot listen on the local MCP endpoint.";
					stopService();
					return;
				}
				m_endpointName = address;
				if (!writeConnectionFile())
				{
					m_lastError = "Cannot securely publish connection file; an existing file is never replaced.";
					stopService();
					return;
				}
				m_listening = true;
			},
			Qt::QueuedConnection);
	});
	m_ioThread.start();

	return true;
}

void AiControlService::stopService()
{
	if (!m_running) { return; }

	m_ready = false;
	m_listening = false;
	m_running = false;

	if (m_server)
	{
		QMetaObject::invokeMethod(
			m_server.get(),
			[this] {
				m_server->stopListening();
				m_ioThread.quit();
			},
			Qt::QueuedConnection);
		m_ioThread.wait();
		m_server.reset();
	}

	AiJobManager::instance()->shutdown();
	AiProjectRevision::instance()->detach();
	AiParameterRegistry::instance()->clear();
	AiChangePlanManager::instance()->clear();
	removeConnectionFile();
	AiObjectRegistry::instance()->invalidateAll();
}

void AiControlService::recordResult(const QString& action, const QJsonObject& result)
{
	m_diagnostics.append(
		QJsonObject{{"action", action}, {"ok", result["ok"]}, {"errorCode", result["error"].toObject()["code"]},
			{"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
	while (m_diagnostics.size() > 64)
	{
		m_diagnostics.removeFirst();
	}
}

void AiControlService::setReady(bool ready)
{
	m_ready = ready;
}

void AiControlService::setGrantedPermissions(Permission permissions)
{
	if (grantedPermissions() == permissions) { return; }
	m_permissions = static_cast<uint32_t>(permissions);
	++m_permissionGeneration;
	if (m_server)
	{
		QMetaObject::invokeMethod(
			m_server.get(),
			[this] {
				if (m_server) { m_server->revokeSessions(); }
			},
			Qt::QueuedConnection);
	}
	AiChangePlanManager::instance()->clear();
}

bool AiControlService::writeConnectionFile()
{
	if (m_connectionFilePath.isEmpty()) { return false; }
	QFileInfo fi(m_connectionFilePath);
	if (fi.exists()) { return false; } // Never replace another instance or an unrelated file.
	if (!fi.dir().mkpath(".")) { return false; }
	QJsonObject object{{"instanceId", m_instanceId}, {"endpoint", m_endpointName}, {"authToken", m_authToken},
		{"ipcProtocolVersion", "1.0"}, {"bridgeVersion", "1.3.0-alpha"}};
	QTemporaryFile file(fi.dir().filePath(".lmms-mcp-XXXXXX"));
	if (!file.open()) { return false; }
	if (!restrictConnectionFile(file)) { return false; }
	const auto bytes = QJsonDocument(object).toJson();
	if (file.write(bytes) != bytes.size() || !file.flush()) { return false; }
	const QString temporaryName = file.fileName();
	file.close();
	// Publish without replacement even if another process creates the target after our precheck.
#ifdef Q_OS_WIN
	return MoveFileW(
		reinterpret_cast<LPCWSTR>(temporaryName.utf16()), reinterpret_cast<LPCWSTR>(m_connectionFilePath.utf16()));
#else
	return ::link(QFile::encodeName(temporaryName).constData(), QFile::encodeName(m_connectionFilePath).constData())
		== 0;
#endif
}

void AiControlService::removeConnectionFile()
{
	if (!m_connectionFilePath.isEmpty() && QFile::exists(m_connectionFilePath))
	{
		QFile file(m_connectionFilePath);
		if (file.open(QIODevice::ReadOnly)
			&& QJsonDocument::fromJson(file.readAll()).object()["instanceId"].toString() == m_instanceId)
		{
			file.close();
			QFile::remove(m_connectionFilePath);
		}
	}
}

} // namespace lmms::ai
