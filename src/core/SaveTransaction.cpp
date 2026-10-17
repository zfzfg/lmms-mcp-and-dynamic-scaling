/*
 * SaveTransaction.cpp - backup generation survives a failed project commit
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "SaveTransaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace lmms
{

namespace
{

SaveIoHooks* g_hooks = nullptr;

SaveTransactionResult failure(QString code, QString message, QStringList artifacts = {})
{
	SaveTransactionResult result;
	result.ok = false;
	result.code = std::move(code);
	result.message = std::move(message);
	result.artifacts = std::move(artifacts);
	return result;
}

bool hooked(SaveIoHooks::Step step)
{
	return g_hooks != nullptr && g_hooks->fail(step);
}

bool copyFile(const QString& from, const QString& to)
{
	if (hooked(SaveIoHooks::Step::CopyProjectToCandidate)) { return false; }
	return QFile::copy(from, to);
}

bool renameFile(const QString& from, const QString& to, SaveIoHooks::Step step)
{
	if (hooked(step)) { return false; }
	return QFile::rename(from, to);
}

bool removeFile(const QString& path, SaveIoHooks::Step step)
{
	if (hooked(step)) { return false; }
	return QFile::remove(path);
}

} // namespace

void SaveTransaction::setHooks(SaveIoHooks* hooks)
{
	g_hooks = hooks;
}

SaveIoHooks* SaveTransaction::hooks()
{
	return g_hooks;
}

QStringList SaveTransaction::listArtifacts(const QString& finalPath)
{
	QStringList artifacts;
	const QFileInfo info(finalPath);
	if (info.fileName().isEmpty() || info.absolutePath().isEmpty()) { return artifacts; }
	QDir dir(info.absolutePath());
	const QString name = info.fileName();
	const auto entries = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
	for (const auto& entry : entries)
	{
		const QString entryName = entry.fileName();
		if (entryName == name + QStringLiteral(".bak")
			|| entryName == name + QStringLiteral(".new")
			|| entryName.startsWith(name + QStringLiteral(".bak.")))
		{
			artifacts.append(entry.absoluteFilePath());
		}
	}
	return artifacts;
}

SaveTransactionResult SaveTransaction::publish(const QString& finalPath, bool createBackup, const Writer& writer)
{
	const QString backupPath = finalPath + QStringLiteral(".bak");
	const QString token = QUuid::createUuid().toString(QUuid::Id128);
	const QString candidatePath = finalPath + QStringLiteral(".bak.incoming-") + token;
	const QString previousPath = finalPath + QStringLiteral(".bak.previous-") + token;
	const QString rollbackHold = finalPath + QStringLiteral(".bak.rollback-") + token;

	bool backupExisted = false;
	bool candidateCreated = false;
	bool previousHeld = false;
	bool backupPublished = false;

	auto artifactsNow = [&]() {
		return listArtifacts(finalPath);
	};

	auto rollbackBackup = [&](QString* rollbackError) -> bool {
		if (!backupPublished) { return true; }
		if (backupExisted)
		{
			// Keep B at previousPath until it is back on .bak. Do not delete it on failure.
			if (!renameFile(backupPath, rollbackHold, SaveIoHooks::Step::RestorePreviousBackup))
			{
				if (rollbackError)
				{
					*rollbackError = QStringLiteral("Could not move the new backup aside. Kept %1 and %2.")
						.arg(backupPath, previousPath);
				}
				return false;
			}
			if (!renameFile(previousPath, backupPath, SaveIoHooks::Step::RestorePreviousBackup))
			{
				renameFile(rollbackHold, backupPath, SaveIoHooks::Step::None);
				if (rollbackError)
				{
					*rollbackError = QStringLiteral("Could not restore the previous backup. Kept %1 and %2.")
						.arg(previousPath, rollbackHold);
				}
				return false;
			}
			previousHeld = false;
			removeFile(rollbackHold, SaveIoHooks::Step::None);
			backupPublished = false;
			return true;
		}
		if (!removeFile(backupPath, SaveIoHooks::Step::RestorePreviousBackup))
		{
			if (rollbackError)
			{
				*rollbackError = QStringLiteral("Could not remove the backup created for this attempt: %1.").arg(backupPath);
			}
			return false;
		}
		backupPublished = false;
		return true;
	};

	auto failSave = [&](QString code, QString message) {
		QString rollbackError;
		const bool rolled = rollbackBackup(&rollbackError);
		QString text = std::move(message);
		if (!rolled)
		{
			text += QStringLiteral(" ");
			text += rollbackError;
		}
		if (candidateCreated) { QFile::remove(candidatePath); }
		auto result = failure(std::move(code), text, artifactsNow());
		return result;
	};

	if (createBackup && QFileInfo::exists(finalPath))
	{
		const QFileInfo existingBackup(backupPath);
		if (existingBackup.exists() && !existingBackup.isFile())
		{
			return failure(QStringLiteral("save.backup"),
				QStringLiteral("Could not replace the backup %1. The project file was left unchanged.").arg(backupPath),
				artifactsNow());
		}
		if (QFileInfo::exists(candidatePath) || QFileInfo::exists(previousPath))
		{
			return failure(QStringLiteral("save.backup"),
				QStringLiteral("A save transaction file already uses %1. The project file was left unchanged.").arg(finalPath),
				artifactsNow());
		}
		if (!copyFile(finalPath, candidatePath))
		{
			QFile::remove(candidatePath);
			return failure(QStringLiteral("save.backup"),
				QStringLiteral("Could not create the backup %1. The project file was left unchanged.").arg(backupPath),
				artifactsNow());
		}
		candidateCreated = true;
		if (existingBackup.exists())
		{
			backupExisted = true;
			if (!renameFile(backupPath, previousPath, SaveIoHooks::Step::MoveBackupAside))
			{
				QFile::remove(candidatePath);
				return failure(QStringLiteral("save.backup"),
					QStringLiteral("Could not preserve the previous backup %1. The project file was left unchanged.").arg(backupPath),
					artifactsNow());
			}
			previousHeld = true;
		}
		if (!renameFile(candidatePath, backupPath, SaveIoHooks::Step::PublishCandidateAsBackup))
		{
			QString restoreError;
			if (previousHeld && !renameFile(previousPath, backupPath, SaveIoHooks::Step::RestorePreviousBackup))
			{
				restoreError = QStringLiteral(" Also failed to restore %1.").arg(previousPath);
			}
			else
			{
				previousHeld = false;
			}
			QFile::remove(candidatePath);
			return failure(QStringLiteral("save.backup"),
				QStringLiteral("Could not publish the backup %1. The project file was left unchanged.%2").arg(backupPath, restoreError),
				artifactsNow());
		}
		candidateCreated = false;
		backupPublished = true;
	}

	QSaveFile outfile(finalPath);
	outfile.setDirectWriteFallback(false);
	if (hooked(SaveIoHooks::Step::Open) || !outfile.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		return failSave(QStringLiteral("save.open"),
			QStringLiteral("Could not open %1 for writing (%2).").arg(finalPath, outfile.errorString()));
	}

	QString writeError;
	const bool wrote = !hooked(SaveIoHooks::Step::Write) && writer && writer(outfile, &writeError);
	if (!wrote)
	{
		outfile.cancelWriting();
		if (writeError.isEmpty()) { writeError = QStringLiteral("Writing %1 failed (%2).").arg(finalPath, outfile.errorString()); }
		return failSave(QStringLiteral("save.write"), writeError);
	}
	if (hooked(SaveIoHooks::Step::Commit) || !outfile.commit())
	{
		return failSave(QStringLiteral("save.commit"),
			QStringLiteral("Could not publish %1 (%2). The previous file, if any, was kept.")
				.arg(finalPath, outfile.errorString()));
	}

	SaveTransactionResult result;
	result.ok = true;
	result.code = QStringLiteral("ok");
	if (previousHeld)
	{
		if (!removeFile(previousPath, SaveIoHooks::Step::CleanupPrevious))
		{
			result.cleanupWarning = true;
			result.message = QStringLiteral("Saved %1. The previous backup generation is still at %2.")
				.arg(finalPath, previousPath);
			result.artifacts = artifactsNow();
		}
	}
	return result;
}

} // namespace lmms
