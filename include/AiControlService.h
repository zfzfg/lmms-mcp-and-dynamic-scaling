#ifndef LMMS_AI_CONTROL_SERVICE_H
#define LMMS_AI_CONTROL_SERVICE_H

#include <QObject>
#include <QString>
#include <memory>
#include "lmms_export.h"
#include "AiDomainTypes.h"

namespace lmms::ai
{

class AiLocalServer;

class LMMS_EXPORT AiControlService : public QObject
{
	Q_OBJECT
public:
	static AiControlService* instance();

	bool startService(const QString& customConnectionFilePath = QString());
	void stopService();

	bool isRunning() const { return m_running; }
	bool isReady() const { return m_ready; }
	void setReady(bool ready);

	QString instanceId() const { return m_instanceId; }
	QString authToken() const { return m_authToken; }
	QString endpointName() const { return m_endpointName; }
	QString connectionFilePath() const { return m_connectionFilePath; }

	Permission grantedPermissions() const { return m_permissions; }
	void setGrantedPermissions(Permission perms) { m_permissions = perms; }

private:
	AiControlService();
	~AiControlService() override;

	void writeConnectionFile();
	void removeConnectionFile();

	bool m_running{false};
	bool m_ready{false};
	QString m_instanceId;
	QString m_authToken;
	QString m_endpointName;
	QString m_connectionFilePath;
	Permission m_permissions{Permission::Read | Permission::WriteProject | Permission::Assets | Permission::Export};

	std::unique_ptr<AiLocalServer> m_server;
};

} // namespace lmms::ai

#endif // LMMS_AI_CONTROL_SERVICE_H
