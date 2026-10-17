/*
 * RecoveryPrompt.cpp - list every recoverable session and name a discard-all action
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "RecoveryPrompt.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace lmms
{
namespace gui
{

RecoveryPromptResult promptRecovery(const QVector<ConfigManager::RecoveryCandidate>& candidates, QWidget* parent)
{
	RecoveryPromptResult result;
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Project recovery"));
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QObject::tr(
		"LMMS found sessions that were not closed cleanly. Restoring or discarding affects only the selected session. "
		"Close leaves every file in place.")));
	auto* list = new QListWidget(&dialog);
	for (const auto& candidate : candidates)
	{
		const QString state = candidate.liveness == ProcessLiveness::Unknown
			? QObject::tr("owner unknown")
			: QObject::tr("owner exited");
		const QString when = candidate.modified.isValid()
			? candidate.modified.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
			: QObject::tr("unknown date");
		auto* item = new QListWidgetItem(QObject::tr("%1 — %2 bytes — %3 — %4")
			.arg(candidate.id, QString::number(candidate.bytes), when, state), list);
		item->setData(Qt::UserRole, candidate.path);
		item->setData(Qt::UserRole + 1, static_cast<int>(candidate.liveness));
	}
	if (list->count() > 0) { list->setCurrentRow(0); }
	layout->addWidget(list);

	auto* buttons = new QHBoxLayout();
	auto* restoreButton = new QPushButton(QObject::tr("Restore selected"));
	auto* laterButton = new QPushButton(QObject::tr("Decide later"));
	auto* discardButton = new QPushButton(QObject::tr("Discard selected"));
	auto* discardAllButton = new QPushButton(QObject::tr("Discard all sessions"));
	laterButton->setDefault(true);
	buttons->addWidget(restoreButton);
	buttons->addWidget(discardButton);
	buttons->addWidget(discardAllButton);
	buttons->addWidget(laterButton);
	layout->addLayout(buttons);

	QObject::connect(laterButton, &QPushButton::clicked, &dialog, &QDialog::reject);
	QObject::connect(&dialog, &QDialog::rejected, &dialog, [&]() { result.choice = RecoveryChoice::Later; });
	QObject::connect(restoreButton, &QPushButton::clicked, &dialog, [&]() {
		auto* item = list->currentItem();
		if (item == nullptr) { return; }
		if (static_cast<ProcessLiveness>(item->data(Qt::UserRole + 1).toInt()) == ProcessLiveness::Unknown)
		{
			QMessageBox::information(&dialog, QObject::tr("Project recovery"),
				QObject::tr("The owner of this session could not be checked, so it was not opened."));
			return;
		}
		result.choice = RecoveryChoice::Restore;
		result.path = item->data(Qt::UserRole).toString();
		dialog.accept();
	});
	QObject::connect(discardButton, &QPushButton::clicked, &dialog, [&]() {
		auto* item = list->currentItem();
		if (item == nullptr) { return; }
		if (static_cast<ProcessLiveness>(item->data(Qt::UserRole + 1).toInt()) == ProcessLiveness::Unknown)
		{
			QMessageBox::information(&dialog, QObject::tr("Project recovery"),
				QObject::tr("The owner of this session could not be checked, so it was not deleted."));
			return;
		}
		result.choice = RecoveryChoice::DiscardOne;
		result.path = item->data(Qt::UserRole).toString();
		dialog.accept();
	});
	QObject::connect(discardAllButton, &QPushButton::clicked, &dialog, [&]() {
		const auto answer = QMessageBox::question(&dialog, QObject::tr("Discard all sessions"),
			QObject::tr("Discard every listed session whose owner has exited? Sessions with an unknown owner are kept."),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes) { return; }
		result.choice = RecoveryChoice::DiscardAll;
		dialog.accept();
	});
	dialog.exec();
	return result;
}

} // namespace gui
} // namespace lmms
