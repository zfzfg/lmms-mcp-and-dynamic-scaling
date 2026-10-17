/*
 * GuiResponsiveness.cpp - event-loop lag sampler for the main thread
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "GuiResponsiveness.h"

namespace lmms::gui
{

GuiResponsiveness::GuiResponsiveness(QObject* parent)
	: QObject(parent)
{
	m_clock.start();
	connect(&m_timer, &QTimer::timeout, this, &GuiResponsiveness::onTick);
	m_timer.start(50);
}

void GuiResponsiveness::onTick()
{
	const auto elapsed = static_cast<int>(m_clock.restart());
	m_lastLagMs = elapsed > 50 ? elapsed - 50 : 0;
	if (m_lastLagMs > m_maxLagMs) { m_maxLagMs = m_lastLagMs; }
}

} // namespace lmms::gui
