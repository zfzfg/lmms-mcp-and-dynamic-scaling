#ifndef LMMS_GUI_AI_CONNECTION_DIALOG_H
#define LMMS_GUI_AI_CONNECTION_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include "lmms_export.h"

namespace lmms::gui
{

class LMMS_EXPORT AiConnectionDialog : public QDialog
{
	Q_OBJECT
public:
	explicit AiConnectionDialog(QWidget* parent = nullptr);
	~AiConnectionDialog() override = default;

private slots:
	void onCopyConfigClicked();
	void onToggleServiceClicked();

private:
	void updateStatusDisplay();

	QLabel* m_statusLabel{nullptr};
	QLineEdit* m_endpointEdit{nullptr};
	QLineEdit* m_fileEdit{nullptr};
	QPushButton* m_toggleBtn{nullptr};
	QPushButton* m_copyBtn{nullptr};
	QCheckBox* m_allowWriteCheck{nullptr};
	QCheckBox* m_allowExportCheck{nullptr};
};

} // namespace lmms::gui

#endif // LMMS_GUI_AI_CONNECTION_DIALOG_H
