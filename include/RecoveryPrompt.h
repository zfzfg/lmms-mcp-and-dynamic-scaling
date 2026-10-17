/*
 * RecoveryPrompt.h - choose one recovery session without deleting the others
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_RECOVERY_PROMPT_H
#define LMMS_RECOVERY_PROMPT_H

#include <QString>

#include "ConfigManager.h"

class QWidget;

namespace lmms
{
namespace gui
{

enum class RecoveryChoice
{
	Later,
	Restore,
	DiscardOne,
	DiscardAll
};

struct RecoveryPromptResult
{
	RecoveryChoice choice = RecoveryChoice::Later;
	QString path;
};

//! Close and Escape select Later and delete nothing.
RecoveryPromptResult promptRecovery(const QVector<ConfigManager::RecoveryCandidate>& candidates, QWidget* parent = nullptr);

} // namespace gui
} // namespace lmms

#endif // LMMS_RECOVERY_PROMPT_H
