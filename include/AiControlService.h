#ifndef LMMS_AI_CONTROL_SERVICE_H
#define LMMS_AI_CONTROL_SERVICE_H

#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <memory>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms::ai {

class AiLocalServer;

class LMMS_EXPORT AiControlService : public QObject
{
	Q_OBJECT
public:
	static AiControlService* instance();

	bool startService(const QString& customConnectionFilePath = QString());
	void stopService();

	bool isRunning() const { return m_running; }
	bool isReady() const { return m_ready && m_listening; }
	void setReady(bool ready);
	QString lastError() const { return m_lastError; }
	QJsonArray diagnostics() const { return m_diagnostics; }
	void recordResult(const QString& action, const QJsonObject& result);

	QString instanceId() const { return m_instanceId; }
	QString authToken() const { return m_authToken; }
	QString endpointName() const { return m_endpointName; }
	QString connectionFilePath() const { return m_connectionFilePath; }

	uint64_t permissionGeneration() const { return m_permissionGeneration; }
	Permission grantedPermissions() const { return static_cast<Permission>(m_permissions.load()); }
	void setGrantedPermissions(Permission perms);

private:
	AiControlService();
	~AiControlService() override;

	bool writeConnectionFile();
	void removeConnectionFile();

	std::atomic_bool m_running{false};
	std::atomic_bool m_ready{false};
	std::atomic_bool m_listening{false};
	QString m_lastError;
	QJsonArray m_diagnostics;
	QString m_instanceId;
	QString m_authToken;
	QString m_endpointName;
	QString m_connectionFilePath;
	std::atomic<uint32_t> m_permissions{
		static_cast<uint32_t>(Permission::Read | Permission::WriteProject | Permission::Assets | Permission::Export)};
	std::atomic<uint64_t> m_permissionGeneration{1};
	// App-thread flag. Set for the whole shutdown so a timed-out stop is not repeated
	// as a fresh one, and cleared only by a later successful start.
	bool m_shuttingDown{false};
	// Heap-allocated so a timed-out thread can be leaked instead of destroyed alive.
	std::unique_ptr<QThread> m_ioThread;

	std::unique_ptr<AiLocalServer> m_server;
};

} // namespace lmms::ai

#endif // LMMS_AI_CONTROL_SERVICE_H
