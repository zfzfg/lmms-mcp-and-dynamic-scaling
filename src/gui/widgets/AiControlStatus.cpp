#include "AiControlStatus.h"
#include "AiControlService.h"
#include "AiConnectionDialog.h"
#include <QHBoxLayout>
#include <QMouseEvent>

namespace lmms::gui
{

AiControlStatus::AiControlStatus(QWidget* parent)
	: QWidget(parent)
{
	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 2);

	m_label = new QLabel(this);
	m_label->setCursor(Qt::PointingHandCursor);
	layout->addWidget(m_label);

	updateStatus();
}

void AiControlStatus::updateStatus()
{
	auto svc = ai::AiControlService::instance();
	if (!svc->isRunning())
	{
		m_label->setText(tr("<span style='color: gray;'>● KI getrennt</span>"));
		setToolTip(tr("KI-Steuerung ist inaktiv. Klicken zum Konfigurieren."));
	}
	else if (svc->isReady())
	{
		m_label->setText(tr("<span style='color: #4CAF50;'>● KI bereit</span>"));
		setToolTip(tr("KI-Steuerung ist aktiv. Klicken für Details."));
	}
	else
	{
		m_label->setText(tr("<span style='color: orange;'>● KI initialisiert</span>"));
		setToolTip(tr("KI-Steuerung wird initialisiert..."));
	}
}

void AiControlStatus::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		AiConnectionDialog dlg(this);
		dlg.exec();
		updateStatus();
	}
	QWidget::mousePressEvent(event);
}

} // namespace lmms::gui
