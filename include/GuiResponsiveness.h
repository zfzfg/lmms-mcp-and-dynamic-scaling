/*
 * GuiResponsiveness.h - event-loop lag sampler for the main thread
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_GUI_RESPONSIVENESS_H
#define LMMS_GUI_RESPONSIVENESS_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace lmms::gui
{

class GuiResponsiveness : public QObject
{
	Q_OBJECT
public:
	explicit GuiResponsiveness(QObject* parent = nullptr);
	auto lastLagMs() const -> int { return m_lastLagMs; }
	auto maxLagMs() const -> int { return m_maxLagMs; }

private slots:
	void onTick();

private:
	QTimer m_timer;
	QElapsedTimer m_clock;
	int m_lastLagMs = 0;
	int m_maxLagMs = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_RESPONSIVENESS_H
