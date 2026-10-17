#include "AiControlStatus.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>

#include "../modals/AiConnectionDialog.h"
#include "AiControlService.h"

namespace lmms::gui {

AiControlStatus::AiControlStatus(QWidget* parent)
	: QWidget(parent)
{
	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 2);

	m_label = new QLabel(this);
	m_label->setCursor(Qt::PointingHandCursor);
	layout->addWidget(m_label);

	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &AiControlStatus::updateStatus);
	timer->start(500);
	connect(ai::AiControlService::instance(), &ai::AiControlService::statusChanged,
		this, &AiControlStatus::updateStatus);
	updateStatus();
}

void AiControlStatus::updateStatus()
{
	auto svc = ai::AiControlService::instance();
	if (!svc->isRunning() && !svc->lastError().isEmpty())
	{
		m_label->setText(tr("<span style='color: red;'>● AI failed</span>"));
		setToolTip(tr("AI control failed: %1").arg(svc->lastError()));
	}
	else if (!svc->isRunning())
	{
		m_label->setText(tr("<span style='color: gray;'>● AI stopped</span>"));
		setToolTip(tr("AI control is stopped. Click to configure."));
	}
	else if (svc->isReady())
	{
		m_label->setText(tr("<span style='color: #4CAF50;'>● AI ready</span>"));
		setToolTip(tr("AI control is ready. Click for details."));
	}
	else
	{
		m_label->setText(tr("<span style='color: orange;'>● AI starting</span>"));
		setToolTip(tr("AI control is starting. Click for details."));
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
