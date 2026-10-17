/*
 * BundleTransaction.h - publish an LMMS project bundle without deleting a foreign directory
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_BUNDLE_TRANSACTION_H
#define LMMS_BUNDLE_TRANSACTION_H

#include <functional>

#include <QString>
#include <QStringList>

namespace lmms
{

//! Two renames are not an atomic directory exchange. The marker records which
//! step finished so a later launch can resume or restore.
enum class BundlePublishState
{
	Prepared,
	OldMoved,
	NewPublished,
	CleanupComplete
};

struct BundleIoHooks
{
	enum class Step
	{
		None,
		CreateStaging,
		CopyResources,
		WriteProject,
		MoveOld,
		PublishNew,
		RollbackOld,
		CleanupOld
	};

	Step failAt = Step::None;
	int failOn = 1;
	int seen = 0;

	bool fail(Step step)
	{
		if (step != failAt) { return false; }
		++seen;
		return seen == failOn;
	}
};

struct BundlePublishResult
{
	bool ok = false;
	bool cleanupWarning = false;
	//! True only when the previous directory was observed back at the canonical path.
	bool restored = false;
	QString code;
	QString message;
	QString publishedPath;
	QString backupPath;
	QString stagingPath;
};

class BundleTransaction
{
public:
	using CopyResources = std::function<bool(const QString& resourcesDir, QString* error)>;
	using WriteProject = std::function<bool(const QString& projectPath, QString* error)>;

	static void setHooks(BundleIoHooks* hooks);

	//! A directory we may replace: not a symlink, and it contains the expected
	//! project file or an LMMS bundle manifest. Any other existing directory is foreign.
	static bool isIdentifiedBundle(const QString& bundleDir, const QString& projectFileName);

	//! Dead Prepared staging may be removed. OldMoved is resumed or restored.
	//! Unknown and live owners are left untouched. Age is not a signal.
	static void recoverAbandoned(const QString& parentPath);

	static BundlePublishResult publish(
		const QString& bundleDir,
		const QString& projectFileName,
		const CopyResources& copyResources,
		const WriteProject& writeProject);
};

} // namespace lmms

#endif // LMMS_BUNDLE_TRANSACTION_H
