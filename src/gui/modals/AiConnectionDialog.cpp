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
	setWindowTitle(tr("KI-Steuerung / MCP Verbindung"));
	setMinimumWidth(450);

	auto layout = new QVBoxLayout(this);

	m_statusLabel = new QLabel(this);
	layout->addWidget(m_statusLabel);

	auto form = new QFormLayout();
	m_endpointEdit = new QLineEdit(this);
	m_endpointEdit->setReadOnly(true);
	form->addRow(tr("Lokaler Endpunkt:"), m_endpointEdit);

	m_fileEdit = new QLineEdit(this);
	m_fileEdit->setReadOnly(true);
	form->addRow(tr("Verbindungsdatei:"), m_fileEdit);
	layout->addLayout(form);

	m_allowWriteCheck = new QCheckBox(tr("Projektbearbeitung durch KI erlauben"), this);
	m_allowWriteCheck->setChecked(
		ai::hasPermission(ai::AiControlService::instance()->grantedPermissions(), ai::Permission::WriteProject));
	layout->addWidget(m_allowWriteCheck);

	m_allowExportCheck = new QCheckBox(tr("Audio-Export durch KI erlauben"), this);
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

	m_copyBtn = new QPushButton(tr("Host-Konfiguration kopieren"), this);
	connect(m_copyBtn, &QPushButton::clicked, this, &AiConnectionDialog::onCopyConfigClicked);
	btnLayout->addWidget(m_copyBtn);

	auto closeBtn = new QPushButton(tr("Schließen"), this);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
	btnLayout->addWidget(closeBtn);

	layout->addLayout(btnLayout);

	updateStatusDisplay();
}

void AiConnectionDialog::updateStatusDisplay()
{
	auto svc = ai::AiControlService::instance();
	if (svc->isRunning())
	{
		m_statusLabel->setText(tr("<b>Status:</b> Dienst aktiv und bereit für KI-Verbindungen."));
		m_endpointEdit->setText(svc->endpointName());
		m_fileEdit->setText(svc->connectionFilePath());
		m_toggleBtn->setText(tr("Dienst beenden"));
		m_copyBtn->setEnabled(true);
	}
	else
	{
		m_statusLabel->setText(
			tr("<b>Status:</b> Dienst ist derzeit deaktiviert.") + " " + svc->lastError().toHtmlEscaped());
		m_endpointEdit->clear();
		m_fileEdit->clear();
		m_toggleBtn->setText(tr("Dienst starten"));
		m_copyBtn->setEnabled(false);
	}
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
		svc->setReady(true);
	}
	updateStatusDisplay();
}

void AiConnectionDialog::onCopyConfigClicked()
{
	auto svc = ai::AiControlService::instance();
	if (!svc->isRunning()) { return; }

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
