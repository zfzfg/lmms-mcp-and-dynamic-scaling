/*
 * RecoveryIdentity.h - versioned process identity for recovery locks
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_RECOVERY_IDENTITY_H
#define LMMS_RECOVERY_IDENTITY_H

#include <QString>

namespace lmms
{

//! Alive: this pid and start token still identify a running process.
//! Dead: the recorded process is gone, or its pid was reused by someone else.
//! Unknown: the operating system refused the query. That is not proof of death.
enum class ProcessLiveness
{
	Alive,
	Dead,
	Unknown
};

struct ProcessIdentity
{
	qint64 pid = 0;
	//! Platform start token. Zero means the lock did not record one.
	quint64 startToken = 0;
	QString uuid;
};

struct RecoveryLockInfo
{
	int version = 0;
	ProcessIdentity identity;
	bool legacyPidOnly = false;
	bool parsed = false;
};

class RecoveryIdentity
{
public:
	static ProcessIdentity currentProcess();
	static ProcessLiveness inspect(const ProcessIdentity& identity);

	static bool writeLockFile(const QString& path, const ProcessIdentity& identity, QString* error);
	static RecoveryLockInfo readLockFile(const QString& path);

	//! Regular file whose canonical path stays inside @p directory and whose name
	//! is `recover.mmp` or `recover-*.mmp`. Symlinks and junctions are rejected.
	static bool isDeletableRecoveryFile(const QString& path, const QString& directory);

	//! Exclusive claim. A live or unknown owner blocks the claim. A confirmed-dead
	//! claim is replaced. The claim file is @p recoveryPath + ".claim".
	static bool tryClaim(const QString& recoveryPath, const ProcessIdentity& who, QString* error);
	static void releaseClaim(const QString& recoveryPath);
};

} // namespace lmms

#endif // LMMS_RECOVERY_IDENTITY_H
