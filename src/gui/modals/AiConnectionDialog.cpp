#include "AiConnectionDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVBoxLayout>

#include "AiControlService.h"

namespace lmms::gui {

AiConnectionDialog::AiConnectionDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("AI Control / MCP Connection"));
	setMinimumWidth(450);

	auto layout = new QVBoxLayout(this);

	m_statusLabel = new QLabel(this);
	layout->addWidget(m_statusLabel);

	auto form = new QFormLayout();
	m_endpointEdit = new QLineEdit(this);
	m_endpointEdit->setReadOnly(true);
	form->addRow(tr("Local endpoint:"), m_endpointEdit);

	m_fileEdit = new QLineEdit(this);
	m_fileEdit->setReadOnly(true);
	form->addRow(tr("Connection file:"), m_fileEdit);
	layout->addLayout(form);

	m_allowWriteCheck = new QCheckBox(tr("Allow AI to edit the project"), this);
	m_allowWriteCheck->setChecked(
		ai::hasPermission(ai::AiControlService::instance()->grantedPermissions(), ai::Permission::WriteProject));
	layout->addWidget(m_allowWriteCheck);

	m_allowExportCheck = new QCheckBox(tr("Allow AI to export audio"), this);
	m_allowExportCheck->setChecked(
		ai::hasPermission(ai::AiControlService::instance()->grantedPermissions(), ai::Permission::Export));
	auto updatePermissions = [this] {
		ai::Permission permissions = ai::Permission::Read | ai::Permission::Assets;
		if (m_allowWriteCheck->isChecked()) { permissions = permissions | ai::Permission::WriteProject; }
		if (m_allowExportCheck->isChecked()) { permissions = permissions | ai::Permission::Export; }
		ai::AiControlService::instance()->setGrantedPermissions(permissions);
	};
	connect(m_allowWriteCheck, &QCheckBox::toggled, this, updatePermissions);
	connect(m_allowExportCheck, &QCheckBox::toggled, this, updatePermissions);
	layout->addWidget(m_allowExportCheck);

	auto btnLayout = new QHBoxLayout();
	m_toggleBtn = new QPushButton(this);
	connect(m_toggleBtn, &QPushButton::clicked, this, &AiConnectionDialog::onToggleServiceClicked);
	btnLayout->addWidget(m_toggleBtn);

	m_copyBtn = new QPushButton(tr("Copy Host Configuration"), this);
	connect(m_copyBtn, &QPushButton::clicked, this, &AiConnectionDialog::onCopyConfigClicked);
	btnLayout->addWidget(m_copyBtn);

	auto closeBtn = new QPushButton(tr("Close"), this);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
	btnLayout->addWidget(closeBtn);

	layout->addLayout(btnLayout);

	connect(ai::AiControlService::instance(), &ai::AiControlService::statusChanged,
		this, &AiConnectionDialog::updateStatusDisplay);
	updateStatusDisplay();
}

void AiConnectionDialog::updateStatusDisplay()
{
	auto svc = ai::AiControlService::instance();
	const bool running = svc->isRunning();
	if (!running && !svc->lastError().isEmpty())
	{
		m_statusLabel->setText(tr("<b>Status:</b> Failed. %1").arg(svc->lastError().toHtmlEscaped()));
	}
	else if (!running) { m_statusLabel->setText(tr("<b>Status:</b> Stopped.")); }
	else if (svc->isReady()) { m_statusLabel->setText(tr("<b>Status:</b> Ready for AI connections.")); }
	else { m_statusLabel->setText(tr("<b>Status:</b> Starting. Waiting for the service and project to be ready.")); }
	m_endpointEdit->setText(svc->isPublished() ? svc->endpointName() : QString());
	m_fileEdit->setText(svc->isPublished() ? svc->connectionFilePath() : QString());
	m_toggleBtn->setText(running ? tr("Stop Service") : tr("Start Service"));
	m_copyBtn->setEnabled(svc->isPublished());
}

void AiConnectionDialog::onToggleServiceClicked()
{
	auto svc = ai::AiControlService::instance();
	if (svc->isRunning()) { svc->stopService(); }
	else
	{
		ai::Permission p = ai::Permission::Read | ai::Permission::Assets;
		if (m_allowWriteCheck->isChecked()) p = p | ai::Permission::WriteProject;
		if (m_allowExportCheck->isChecked()) p = p | ai::Permission::Export;
		svc->setGrantedPermissions(p);
		svc->startService();
	}
	updateStatusDisplay();
}

void AiConnectionDialog::onCopyConfigClicked()
{
	auto svc = ai::AiControlService::instance();
	if (!svc->isPublished()) { return; }

	QJsonObject mcpServer;
#ifdef Q_OS_WIN
	mcpServer["command"] = QCoreApplication::applicationDirPath() + "/lmms-mcp.exe";
#else
	mcpServer["command"] = QCoreApplication::applicationDirPath() + "/lmms-mcp";
#endif
	QJsonArray args;
	args.append("--connection-file");
	args.append(svc->connectionFilePath());
	mcpServer["args"] = args;

	QJsonObject servers;
	servers["lmms"] = mcpServer;
	QJsonObject root;
	root["mcpServers"] = servers;

	QString jsonText = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
	QClipboard* clip = QApplication::clipboard();
	if (clip) { clip->setText(jsonText); }
}

} // namespace lmms::gui
