/*
 * DiagnosticReport.h - local support report without project contents or secrets
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_DIAGNOSTIC_REPORT_H
#define LMMS_DIAGNOSTIC_REPORT_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{

struct LMMS_EXPORT DiagnosticExtras
{
	int guiLagMs = -1;
	int guiLagMaxMs = -1;
};

class LMMS_EXPORT DiagnosticReport
{
public:
	static auto text(DiagnosticExtras extras = {}) -> QString;
	static auto writeTo(const QString& path, QString* error, DiagnosticExtras extras = {}) -> bool;
};

} // namespace lmms

#endif // LMMS_DIAGNOSTIC_REPORT_H
