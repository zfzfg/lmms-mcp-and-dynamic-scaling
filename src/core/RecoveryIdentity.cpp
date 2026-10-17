/*
 * RecoveryIdentity.cpp - pid plus start token, with unknown distinct from dead
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "RecoveryIdentity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <QFile>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace lmms
{

namespace
{

#if defined(__linux__)
bool readProcStart(qint64 pid, quint64* startToken)
{
	QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
	if (!statFile.open(QIODevice::ReadOnly)) { return false; }
	const QByteArray text = statFile.readAll();
	const int closeParen = text.lastIndexOf(')');
	if (closeParen < 0 || closeParen + 2 >= text.size()) { return false; }
	const QList<QByteArray> fields = text.mid(closeParen + 2).simplified().split(' ');
	// starttime is field 22 of stat, and field 20 of the tokens that follow comm.
	if (fields.size() < 20) { return false; }
	bool ok = false;
	const quint64 value = fields.at(19).toULongLong(&ok);
	if (!ok) { return false; }
	*startToken = value;
	return true;
}
#endif

ProcessLiveness inspectPlatform(const ProcessIdentity& identity)
{
	if (identity.pid <= 0) { return ProcessLiveness::Unknown; }
#if defined(_WIN32)
	HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(identity.pid));
	if (!handle)
	{
		return GetLastError() == ERROR_ACCESS_DENIED ? ProcessLiveness::Unknown : ProcessLiveness::Dead;
	}
	FILETIME created{};
	FILETIME exited{};
	FILETIME kernel{};
	FILETIME user{};
	const BOOL times = GetProcessTimes(handle, &created, &exited, &kernel, &user);
	CloseHandle(handle);
	if (!times) { return ProcessLiveness::Unknown; }
	ULARGE_INTEGER stamp;
	stamp.LowPart = created.dwLowDateTime;
	stamp.HighPart = created.dwHighDateTime;
	if (identity.startToken != 0 && stamp.QuadPart != identity.startToken) { return ProcessLiveness::Dead; }
	return ProcessLiveness::Alive;
#elif defined(__linux__)
	if (::kill(static_cast<pid_t>(identity.pid), 0) != 0)
	{
		if (errno == EPERM) { return ProcessLiveness::Unknown; }
		return ProcessLiveness::Dead;
	}
	quint64 observed = 0;
	if (!readProcStart(identity.pid, &observed))
	{
		return identity.startToken == 0 ? ProcessLiveness::Alive : ProcessLiveness::Unknown;
	}
	if (identity.startToken != 0 && observed != identity.startToken) { return ProcessLiveness::Dead; }
	return ProcessLiveness::Alive;
#else
	if (::kill(static_cast<pid_t>(identity.pid), 0) != 0)
	{
		if (errno == EPERM) { return ProcessLiveness::Unknown; }
		return ProcessLiveness::Dead;
	}
	// Without a start token there is no way to see pid reuse. A recorded token
	// that cannot be compared must stay unknown.
	if (identity.startToken != 0) { return ProcessLiveness::Unknown; }
	return ProcessLiveness::Alive;
#endif
}

} // namespace

ProcessIdentity RecoveryIdentity::currentProcess()
{
	ProcessIdentity identity;
	identity.uuid = QUuid::createUuid().toString(QUuid::Id128);
#if defined(_WIN32)
	identity.pid = static_cast<qint64>(GetCurrentProcessId());
	FILETIME created{};
	FILETIME exited{};
	FILETIME kernel{};
	FILETIME user{};
	if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
	{
		ULARGE_INTEGER stamp;
		stamp.LowPart = created.dwLowDateTime;
		stamp.HighPart = created.dwHighDateTime;
		identity.startToken = stamp.QuadPart;
	}
#elif defined(__linux__)
	identity.pid = static_cast<qint64>(::getpid());
	quint64 start = 0;
	if (readProcStart(identity.pid, &start)) { identity.startToken = start; }
#else
	identity.pid = static_cast<qint64>(::getpid());
#endif
	return identity;
}

ProcessLiveness RecoveryIdentity::inspect(const ProcessIdentity& identity)
{
	if (identity.pid == currentProcess().pid)
	{
		const auto self = currentProcess();
		if (identity.startToken == 0 || identity.startToken == self.startToken) { return ProcessLiveness::Alive; }
	}
	return inspectPlatform(identity);
}

bool RecoveryIdentity::writeLockFile(const QString& path, const ProcessIdentity& identity, QString* error)
{
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error) { *error = file.errorString(); }
		return false;
	}
	const QByteArray body = QStringLiteral("lmms-lock 2\npid %1\nstart %2\nuuid %3\n")
		.arg(identity.pid)
		.arg(identity.startToken)
		.arg(identity.uuid)
		.toUtf8();
	if (file.write(body) != body.size() || !file.commit())
	{
		if (error) { *error = file.errorString(); }
		return false;
	}
	return true;
}

RecoveryLockInfo RecoveryIdentity::readLockFile(const QString& path)
{
	RecoveryLockInfo info;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return info; }
	const QString text = QString::fromUtf8(file.readAll());
	const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
	if (lines.isEmpty()) { return info; }
	if (lines.first().startsWith(QStringLiteral("lmms-lock ")))
	{
		info.version = lines.first().section(' ', 1, 1).toInt();
		for (int index = 1; index < lines.size(); ++index)
		{
			const QString key = lines.at(index).section(' ', 0, 0);
			const QString value = lines.at(index).section(' ', 1);
			if (key == QStringLiteral("pid")) { info.identity.pid = value.toLongLong(); }
			else if (key == QStringLiteral("start")) { info.identity.startToken = value.toULongLong(); }
			else if (key == QStringLiteral("uuid")) { info.identity.uuid = value.trimmed(); }
		}
		info.parsed = info.identity.pid > 0;
		return info;
	}
	bool ok = false;
	info.identity.pid = lines.first().trimmed().toLongLong(&ok);
	if (lines.size() > 1) { info.identity.uuid = lines.at(1).trimmed(); }
	info.legacyPidOnly = true;
	info.version = 1;
	info.parsed = ok && info.identity.pid > 0;
	return info;
}

bool RecoveryIdentity::isDeletableRecoveryFile(const QString& path, const QString& directory)
{
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile() || info.isSymLink()) { return false; }
	const QString name = info.fileName();
	const bool legacy = name == QStringLiteral("recover.mmp");
	const bool named = name.startsWith(QStringLiteral("recover-")) && name.endsWith(QStringLiteral(".mmp"))
		&& !name.contains(QStringLiteral(".."));
	if (!legacy && !named) { return false; }
	const QString canonical = QDir::cleanPath(info.canonicalFilePath());
	const QString root = QDir::cleanPath(QFileInfo(directory).canonicalFilePath());
	if (canonical.isEmpty() || root.isEmpty()) { return false; }
	const QString parent = QDir::cleanPath(QFileInfo(canonical).absolutePath());
	return parent.compare(root, Qt::CaseInsensitive) == 0;
}

bool RecoveryIdentity::tryClaim(const QString& recoveryPath, const ProcessIdentity& who, QString* error)
{
	const QString claimPath = recoveryPath + QStringLiteral(".claim");
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		QFile claim(claimPath);
		if (claim.open(QIODevice::WriteOnly | QIODevice::NewOnly))
		{
			const QByteArray body = QStringLiteral("lmms-lock 2\npid %1\nstart %2\nuuid %3\n")
				.arg(who.pid)
				.arg(who.startToken)
				.arg(who.uuid)
				.toUtf8();
			const bool wrote = claim.write(body) == body.size();
			claim.close();
			if (!wrote)
			{
				QFile::remove(claimPath);
				if (error) { *error = QStringLiteral("Could not write the recovery claim."); }
				return false;
			}
			return true;
		}
		const auto existing = readLockFile(claimPath);
		if (!existing.parsed)
		{
			if (error) { *error = QStringLiteral("The recovery claim at %1 is unreadable.").arg(claimPath); }
			return false;
		}
		const auto liveness = inspect(existing.identity);
		if (liveness != ProcessLiveness::Dead)
		{
			if (error)
			{
				*error = liveness == ProcessLiveness::Unknown
					? QStringLiteral("The recovery claim owner cannot be queried. The file was left untouched.")
					: QStringLiteral("Another LMMS instance already claimed this recovery file.");
			}
			return false;
		}
		QFile::remove(claimPath);
	}
	if (error) { *error = QStringLiteral("Could not claim the recovery file."); }
	return false;
}

void RecoveryIdentity::releaseClaim(const QString& recoveryPath)
{
	const QString claimPath = recoveryPath + QStringLiteral(".claim");
	const auto existing = readLockFile(claimPath);
	if (!existing.parsed) { return; }
	const auto self = currentProcess();
	if (existing.identity.pid == self.pid && (existing.identity.startToken == 0 || existing.identity.startToken == self.startToken))
	{
		QFile::remove(claimPath);
	}
}

} // namespace lmms
