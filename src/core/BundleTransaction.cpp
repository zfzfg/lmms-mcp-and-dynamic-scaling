/*
 * BundleTransaction.cpp - staged bundle publish with an explicit crash window
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "BundleTransaction.h"

#include "RecoveryIdentity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace lmms
{

namespace
{

BundleIoHooks* g_hooks = nullptr;

bool hooked(BundleIoHooks::Step step)
{
	return g_hooks != nullptr && g_hooks->fail(step);
}

QString stateName(BundlePublishState state)
{
	switch (state)
	{
	case BundlePublishState::Prepared: return QStringLiteral("Prepared");
	case BundlePublishState::OldMoved: return QStringLiteral("OldMoved");
	case BundlePublishState::NewPublished: return QStringLiteral("NewPublished");
	case BundlePublishState::CleanupComplete: return QStringLiteral("CleanupComplete");
	}
	return QStringLiteral("Prepared");
}

BundlePublishState stateFromName(const QString& name)
{
	if (name == QStringLiteral("OldMoved")) { return BundlePublishState::OldMoved; }
	if (name == QStringLiteral("NewPublished")) { return BundlePublishState::NewPublished; }
	if (name == QStringLiteral("CleanupComplete")) { return BundlePublishState::CleanupComplete; }
	return BundlePublishState::Prepared;
}

struct Marker
{
	bool parsed = false;
	QString id;
	qint64 pid = 0;
	quint64 start = 0;
	QString target;
	BundlePublishState state = BundlePublishState::Prepared;
	QString backup;
	QString staging;
};

Marker readMarker(const QString& path)
{
	Marker marker;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return marker; }
	const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
	if (lines.isEmpty() || !lines.first().startsWith(QStringLiteral("lmms-bundle-staging "))) { return marker; }
	for (int index = 1; index < lines.size(); ++index)
	{
		const QString key = lines.at(index).section(' ', 0, 0);
		const QString value = lines.at(index).section(' ', 1);
		if (key == QStringLiteral("id")) { marker.id = value.trimmed(); }
		else if (key == QStringLiteral("pid")) { marker.pid = value.toLongLong(); }
		else if (key == QStringLiteral("start")) { marker.start = value.toULongLong(); }
		else if (key == QStringLiteral("target")) { marker.target = value.trimmed(); }
		else if (key == QStringLiteral("state")) { marker.state = stateFromName(value.trimmed()); }
		else if (key == QStringLiteral("backup")) { marker.backup = value.trimmed(); }
		else if (key == QStringLiteral("staging")) { marker.staging = value.trimmed(); }
	}
	marker.parsed = !marker.id.isEmpty() && !marker.staging.isEmpty();
	return marker;
}

bool writeMarker(const QString& staging, const Marker& marker)
{
	QSaveFile file(staging + QStringLiteral("/.lmms-bundle-staging"));
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const QByteArray body = QStringLiteral(
		"lmms-bundle-staging 1\nid %1\npid %2\nstart %3\ntarget %4\nstate %5\nbackup %6\nstaging %7\n")
		.arg(marker.id)
		.arg(marker.pid)
		.arg(marker.start)
		.arg(marker.target)
		.arg(stateName(marker.state))
		.arg(marker.backup)
		.arg(marker.staging)
		.toUtf8();
	return file.write(body) == body.size() && file.commit();
}

bool sameDirectory(const QString& left, const QString& right)
{
	return QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

bool removeOwnedTree(const QString& path)
{
	const QFileInfo info(path);
	if (!info.exists()) { return true; }
	if (info.isSymLink() || !info.isDir()) { return false; }
	return QDir(path).removeRecursively();
}

bool directorySafeToMove(const QString& path)
{
	const QFileInfo info(path);
	return info.exists() && info.isDir() && !info.isSymLink();
}

BundlePublishResult failed(QString code, QString message, const QString& bundleDir, const QString& staging, const QString& backup)
{
	BundlePublishResult result;
	result.ok = false;
	result.code = std::move(code);
	result.message = std::move(message);
	result.publishedPath = bundleDir;
	result.stagingPath = staging;
	result.backupPath = backup;
	return result;
}

} // namespace

void BundleTransaction::setHooks(BundleIoHooks* hooks)
{
	g_hooks = hooks;
}

bool BundleTransaction::isIdentifiedBundle(const QString& bundleDir, const QString& projectFileName)
{
	const QFileInfo info(bundleDir);
	if (!info.exists() || !info.isDir() || info.isSymLink()) { return false; }
	const QString base = QFileInfo(projectFileName).completeBaseName();
	if (base.isEmpty() || QFileInfo(projectFileName).fileName() != projectFileName) { return false; }
	if (QFileInfo::exists(info.filePath() + QStringLiteral("/.lmms-bundle"))) { return true; }
	const QStringList suffixes{QStringLiteral("mmp"), QStringLiteral("mmpz"), QStringLiteral("mpt")};
	for (const auto& suffix : suffixes)
	{
		if (QFileInfo::exists(info.filePath() + QLatin1Char('/') + base + QLatin1Char('.') + suffix)) { return true; }
	}
	return false;
}

void BundleTransaction::recoverAbandoned(const QString& parentPath)
{
	QDir parent(parentPath);
	if (!parent.exists()) { return; }
	const auto entries = parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto& entry : entries)
	{
		if (entry.isSymLink() || !entry.fileName().contains(QStringLiteral(".staging-"))) { continue; }
		const QString markerPath = entry.filePath() + QStringLiteral("/.lmms-bundle-staging");
		if (!QFileInfo::exists(markerPath)) { continue; }
		const Marker marker = readMarker(markerPath);
		if (!marker.parsed || !sameDirectory(marker.staging, entry.filePath())) { continue; }
		ProcessIdentity owner;
		owner.pid = marker.pid;
		owner.startToken = marker.start;
		const auto liveness = RecoveryIdentity::inspect(owner);
		if (liveness != ProcessLiveness::Dead) { continue; }

		if (marker.state == BundlePublishState::Prepared && marker.backup.isEmpty())
		{
			// The canonical folder was not moved. Staging belongs to a dead attempt.
			removeOwnedTree(entry.filePath());
			continue;
		}
		if (marker.state == BundlePublishState::OldMoved && !QFileInfo::exists(marker.target))
		{
			if (directorySafeToMove(entry.filePath()) && QDir().rename(entry.filePath(), marker.target))
			{
				continue;
			}
			if (directorySafeToMove(marker.backup) && !QFileInfo::exists(marker.target))
			{
				QDir().rename(marker.backup, marker.target);
			}
		}
	}
}

BundlePublishResult BundleTransaction::publish(
	const QString& bundleDir,
	const QString& projectFileName,
	const CopyResources& copyResources,
	const WriteProject& writeProject)
{
	const QFileInfo targetInfo(bundleDir);
	if (targetInfo.isSymLink())
	{
		return failed(QStringLiteral("save.bundle.symlink"),
			QStringLiteral("Refusing to replace a symlink or junction at %1.").arg(bundleDir),
			bundleDir, QString(), QString());
	}
	recoverAbandoned(targetInfo.absolutePath());

	const auto owner = RecoveryIdentity::currentProcess();
	const QString lockPath = bundleDir + QStringLiteral(".publish-lock");
	QFile exclusive(lockPath);
	if (!exclusive.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		const auto lock = RecoveryIdentity::readLockFile(lockPath);
		const auto liveness = lock.parsed ? RecoveryIdentity::inspect(lock.identity) : ProcessLiveness::Unknown;
		if (liveness != ProcessLiveness::Dead)
		{
			return failed(liveness == ProcessLiveness::Unknown
					? QStringLiteral("save.bundle.unknown-owner")
					: QStringLiteral("save.bundle.busy"),
				QStringLiteral("Another publish owns %1. This export was not started.").arg(bundleDir),
				bundleDir, QString(), QString());
		}
		QFile::remove(lockPath);
		if (!exclusive.open(QIODevice::WriteOnly | QIODevice::NewOnly))
		{
			return failed(QStringLiteral("save.bundle.lock"),
				QStringLiteral("Could not lock %1.").arg(bundleDir),
				bundleDir, QString(), QString());
		}
	}
	{
		const QByteArray body = QStringLiteral("lmms-lock 2\npid %1\nstart %2\nuuid %3\n")
			.arg(owner.pid)
			.arg(owner.startToken)
			.arg(owner.uuid)
			.toUtf8();
		if (exclusive.write(body) != body.size())
		{
			exclusive.close();
			QFile::remove(lockPath);
			return failed(QStringLiteral("save.bundle.lock"),
				QStringLiteral("Could not write the publish lock for %1.").arg(bundleDir),
				bundleDir, QString(), QString());
		}
		exclusive.close();
	}

	auto releaseLock = [&]() {
		const auto current = RecoveryIdentity::readLockFile(lockPath);
		if (current.parsed && current.identity.uuid == owner.uuid) { QFile::remove(lockPath); }
	};

	const QString id = QUuid::createUuid().toString(QUuid::Id128);
	const QString staging = bundleDir + QStringLiteral(".staging-") + id;
	const QString backup = bundleDir + QStringLiteral(".bundle-backup-") + id;
	Marker marker;
	marker.id = id;
	marker.pid = owner.pid;
	marker.start = owner.startToken;
	marker.target = QFileInfo(bundleDir).absoluteFilePath();
	marker.staging = QFileInfo(staging).absoluteFilePath();
	marker.state = BundlePublishState::Prepared;

	if (hooked(BundleIoHooks::Step::CreateStaging) || !QDir().mkpath(staging + QStringLiteral("/resources")) || !writeMarker(staging, marker))
	{
		removeOwnedTree(staging);
		releaseLock();
		return failed(QStringLiteral("save.bundle.stage"),
			QStringLiteral("Could not create a staging folder for the project bundle."),
			bundleDir, staging, QString());
	}

	auto abortPrepared = [&](QString code, QString message) {
		removeOwnedTree(staging);
		releaseLock();
		return failed(std::move(code), std::move(message), bundleDir, QString(), QString());
	};

	QString stepError;
	if (hooked(BundleIoHooks::Step::CopyResources) || !copyResources || !copyResources(staging + QStringLiteral("/resources"), &stepError))
	{
		return abortPrepared(QStringLiteral("save.bundle.resources"),
			stepError.isEmpty() ? QStringLiteral("Could not copy every project resource. No bundle was published.") : stepError);
	}
	const QString projectPath = staging + QLatin1Char('/') + projectFileName;
	if (hooked(BundleIoHooks::Step::WriteProject) || !writeProject || !writeProject(projectPath, &stepError))
	{
		return abortPrepared(QStringLiteral("save.bundle.write"),
			stepError.isEmpty() ? QStringLiteral("Could not write the staged project. No bundle was published.") : stepError);
	}
	const QFileInfo stagedProject(projectPath);
	if (!stagedProject.exists() || !stagedProject.isFile() || stagedProject.size() <= 0)
	{
		return abortPrepared(QStringLiteral("save.bundle.verify"),
			QStringLiteral("The staged project file is missing or empty. No bundle was published."));
	}

	QFile manifest(staging + QStringLiteral("/.lmms-bundle"));
	if (manifest.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		manifest.write(QStringLiteral("version=1\nproject=%1\n").arg(projectFileName).toUtf8());
		manifest.close();
	}
	QFile::remove(staging + QStringLiteral("/.lmms-bundle-staging"));

	const bool targetExists = QFileInfo::exists(bundleDir);
	if (targetExists)
	{
		if (!isIdentifiedBundle(bundleDir, projectFileName))
		{
			return abortPrepared(QStringLiteral("save.bundle.foreign"),
				QStringLiteral("The folder %1 is not an LMMS bundle. It was not replaced.").arg(bundleDir));
		}
		if (QFileInfo(bundleDir).isSymLink() || !QFileInfo(bundleDir).isDir())
		{
			return abortPrepared(QStringLiteral("save.bundle.foreign"),
				QStringLiteral("Refusing to replace %1.").arg(bundleDir));
		}
		marker.backup = QFileInfo(backup).absoluteFilePath();
		marker.state = BundlePublishState::OldMoved;
		// Record the intent before the canonical name disappears.
		if (!QDir().mkpath(staging) || !writeMarker(staging, marker))
		{
			return abortPrepared(QStringLiteral("save.bundle.stage"),
				QStringLiteral("Could not record the bundle replacement. The existing folder was left unchanged."));
		}
		if (hooked(BundleIoHooks::Step::MoveOld) || !QDir().rename(bundleDir, backup))
		{
			return abortPrepared(QStringLiteral("save.bundle.move"),
				QStringLiteral("Could not move the existing bundle aside. It was left at %1.").arg(bundleDir));
		}
		if (hooked(BundleIoHooks::Step::PublishNew) || !QDir().rename(staging, bundleDir))
		{
			const bool rolled = !hooked(BundleIoHooks::Step::RollbackOld) && QDir().rename(backup, bundleDir);
			BundlePublishResult result = failed(QStringLiteral("save.bundle.publish"),
				rolled
					? QStringLiteral("Could not publish %1. The previous bundle was restored.").arg(bundleDir)
					: QStringLiteral("Could not publish %1 and could not restore the previous folder. Kept %2 and %3.")
						.arg(bundleDir, backup, staging),
				bundleDir, staging, backup);
			result.restored = rolled && QFileInfo::exists(bundleDir);
			if (!result.restored)
			{
				result.message = QStringLiteral("Could not publish %1 and could not restore the previous folder. Kept %2 and %3.")
					.arg(bundleDir, backup, staging);
			}
			releaseLock();
			return result;
		}
		QFile::remove(bundleDir + QStringLiteral("/.lmms-bundle-staging"));
		if (hooked(BundleIoHooks::Step::CleanupOld) || !removeOwnedTree(backup))
		{
			releaseLock();
			BundlePublishResult result;
			result.ok = true;
			result.cleanupWarning = true;
			result.code = QStringLiteral("ok");
			result.publishedPath = bundleDir;
			result.backupPath = backup;
			result.message = QStringLiteral("Published %1. The previous bundle is still at %2.").arg(bundleDir, backup);
			return result;
		}
	}
	else if (hooked(BundleIoHooks::Step::PublishNew) || !QDir().rename(staging, bundleDir))
	{
		releaseLock();
		return failed(QStringLiteral("save.bundle.publish"),
			QStringLiteral("Could not publish the bundle folder %1. The staging folder was kept at %2.").arg(bundleDir, staging),
			bundleDir, staging, QString());
	}

	QFile::remove(bundleDir + QStringLiteral("/.lmms-bundle-staging"));
	releaseLock();
	BundlePublishResult result;
	result.ok = true;
	result.code = QStringLiteral("ok");
	result.publishedPath = bundleDir;
	return result;
}

} // namespace lmms
