/*
 * SaveTransaction.h - keep the previous backup generation until the new file commits
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_SAVE_TRANSACTION_H
#define LMMS_SAVE_TRANSACTION_H

#include <functional>

#include <QString>
#include <QStringList>

class QSaveFile;

namespace lmms
{

//! Injected failures for tests. A null hook pointer means every operation is real.
//! Hooks are process-global and not safe for concurrent saves.
struct SaveIoHooks
{
	enum class Step
	{
		None,
		CopyProjectToCandidate,
		MoveBackupAside,
		PublishCandidateAsBackup,
		Open,
		Write,
		Commit,
		RestorePreviousBackup,
		CleanupPrevious
	};

	Step failAt = Step::None;
	//! 1-based count of the matching step that should fail.
	int failOn = 1;
	int seen = 0;

	bool fail(Step step)
	{
		if (step != failAt) { return false; }
		++seen;
		return seen == failOn;
	}
};

struct SaveTransactionResult
{
	bool ok = false;
	//! True when the project file was published but a leftover backup artifact remains.
	bool cleanupWarning = false;
	QString code;
	QString message;
	QStringList artifacts;
};

//! Publishes @p finalPath.
//!
//! Before: project A and optional backup B. The writer produces C.
//! Success: A is replaced by C and, when A existed, `.bak` holds A.
//! Failure before commit: A is unchanged. An existing B is restored to `.bak`
//! unless that rollback itself fails, in which case both generations are kept
//! and named in @c artifacts. A missing B is not replaced by a promised older
//! generation.
//!
//! QSaveFile publishes one file. This is not joint atomicity of the project
//! and its backup, and it is not a power-loss guarantee on every filesystem.
class SaveTransaction
{
public:
	using Writer = std::function<bool(QSaveFile&, QString*)>;

	static void setHooks(SaveIoHooks* hooks);
	static SaveIoHooks* hooks();

	static QStringList listArtifacts(const QString& finalPath);

	static SaveTransactionResult publish(const QString& finalPath, bool createBackup, const Writer& writer);
};

} // namespace lmms

#endif // LMMS_SAVE_TRANSACTION_H
