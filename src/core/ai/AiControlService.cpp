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
	if (reopened == INVALID_HANDLE_VALUE)
	{
		qWarning("Cannot reopen MCP connection file to restrict access (Windows error %lu).", GetLastError());
	}
	const bool secured = reopened != INVALID_HANDLE_VALUE
		&& SetSecurityInfo(reopened, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
			   nullptr, nullptr, acl, nullptr)
			== ERROR_SUCCESS;
	if (!secured && reopened != INVALID_HANDLE_VALUE) { qWarning("Cannot restrict MCP connection file access."); }
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
	// A thread that missed the shutdown deadline is still inside Qt. Destroying
	// it, or the server that lives on it, is undefined. Leak both instead.
	if (m_ioThread && m_ioThread->isRunning())
	{
		m_server.release();
		m_ioThread.release();
	}
}

bool AiControlService::startService(const QString& customConnectionFilePath)
{
	if (m_running) { return true; }
	if (m_ioThread && m_ioThread->isRunning())
	{
		m_lastError = "MCP shutdown timed out";
		emit statusChanged();
		return false;
	}

	m_shuttingDown = false;
	const auto generation = ++m_serviceGeneration;
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
	m_ioThread = std::make_unique<QThread>();
	m_server = std::make_unique<AiLocalServer>(m_instanceId, m_authToken);
	m_server->moveToThread(m_ioThread.get());
	m_running = true;
	auto* server = m_server.get();
	const auto endpoint = m_endpointName;
	connect(m_ioThread.get(), &QThread::started, server, [this, server, endpoint, generation] {
		const bool listening = server->startListening(endpoint);
		const QString address = server->serverName();
		QMetaObject::invokeMethod(
			this,
			[this, listening, address, generation] {
				if (!m_running || generation != m_serviceGeneration) { return; }
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
				emit statusChanged();
			},
			Qt::QueuedConnection);
	});
	m_ioThread->start();
	emit statusChanged();

	return true;
}

void AiControlService::stopService()
{
	const bool threadRunning = m_ioThread && m_ioThread->isRunning();
	if (!m_running && !m_shuttingDown && !m_server && !threadRunning) { return; }

	const bool firstStop = !m_shuttingDown;
	++m_serviceGeneration;
	m_listening = false;
	m_running = false;
	m_shuttingDown = true;

	// Unblock dispatcher waiters before joining the I/O thread. Each callback
	// runs once; new dispatch calls are rejected until the next startService.
	if (firstStop) { AiCommandDispatcher::instance()->beginShutdown(); }

	if (m_server && m_ioThread && m_ioThread->isRunning())
	{
		AiLocalServer* server = m_server.get();
		QThread* thread = m_ioThread.get();
		QMetaObject::invokeMethod(
			server,
			[server, thread] {
				server->stopListening();
				thread->quit();
			},
			Qt::QueuedConnection);
		if (!thread->wait(3000))
		{
			m_lastError = "MCP shutdown timed out";
			// The server still lives on the running thread. Do not reset() it here.
		}
		else
		{
			// The thread has exited, so destroying its server from here is safe.
			m_server.reset();
			m_ioThread.reset();
		}
	}
	else if (m_ioThread && m_ioThread->isRunning())
	{
		m_ioThread->quit();
		if (!m_ioThread->wait(3000)) { m_lastError = "MCP shutdown timed out"; }
		else { m_ioThread.reset(); }
	}
	else
	{
		m_server.reset();
		m_ioThread.reset();
	}

	emit statusChanged();
	if (!firstStop) { return; }
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
	if (m_ready.exchange(ready) != ready) { emit statusChanged(); }
}

void AiControlService::setGrantedPermissions(Permission permissions)
{
	if (grantedPermissions() == permissions) { return; }
	m_permissions = static_cast<uint32_t>(permissions);
	++m_permissionGeneration;
	if (m_server)
	{
		auto* server = m_server.get();
		QMetaObject::invokeMethod(
			server,
			[server] { server->revokeSessions(); },
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
	QString temporaryName;
	{
		QTemporaryFile file(fi.dir().filePath(".lmms-mcp-XXXXXX"));
		if (!file.open()) { return false; }
		if (!restrictConnectionFile(file)) { return false; }
		const auto bytes = QJsonDocument(object).toJson();
		if (file.write(bytes) != bytes.size() || !file.flush()) { return false; }
		temporaryName = file.fileName();
		file.setAutoRemove(false);
		// QTemporaryFile::close() retains its native handle. Destruction releases
		// it before publication, so Windows can rename the secured file.
	}
	// Publish without replacement even if another process creates the target after our precheck.
#ifdef Q_OS_WIN
	const bool published = MoveFileW(
		reinterpret_cast<LPCWSTR>(temporaryName.utf16()), reinterpret_cast<LPCWSTR>(m_connectionFilePath.utf16()));
	if (!published) { qWarning("Cannot publish MCP connection file (Windows error %lu).", GetLastError()); }
#else
	const bool published = ::link(QFile::encodeName(temporaryName).constData(), QFile::encodeName(m_connectionFilePath).constData())
		== 0;
#endif
	QFile::remove(temporaryName);
	return published;
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
