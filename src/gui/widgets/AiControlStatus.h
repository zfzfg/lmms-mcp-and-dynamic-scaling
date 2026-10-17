#ifndef LMMS_GUI_AI_CONTROL_STATUS_H
#define LMMS_GUI_AI_CONTROL_STATUS_H

#include <QWidget>
#include <QLabel>
#include "lmms_export.h"

namespace lmms::gui
{

class LMMS_EXPORT AiControlStatus : public QWidget
{
	Q_OBJECT
public:
	explicit AiControlStatus(QWidget* parent = nullptr);
	~AiControlStatus() override = default;

public slots:
	void updateStatus();

protected:
	void mousePressEvent(QMouseEvent* event) override;

private:
	QLabel* m_label{nullptr};
};

} // namespace lmms::gui

#endif // LMMS_GUI_AI_CONTROL_STATUS_H
