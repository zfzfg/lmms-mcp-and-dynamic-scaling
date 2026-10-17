/*
 * FileSearchJob.h
 *
 * Copyright (c) 2025 saker <sakertooth@gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_GUI_FILE_SEARCH_JOB_H
#define LMMS_GUI_FILE_SEARCH_JOB_H

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>

namespace lmms::gui {
//! The `FileSearchJob` class allows for searching for files on the filesystem.
//! Searching occurs on a background thread. Results are batched and tagged with the caller's generation.
//! `search()`, `cancel()`, and the destructor do not wait for filesystem I/O.
//! A search that outlives this object drops its results.
class FileSearchJob : public QObject
{
	Q_OBJECT
public:
	//! Represents a search task to be carried out by the search job.
	struct Task
	{
		QString filter;					 //! The filter to be tokenized.
		QStringList paths;				 //! The list of paths to search recursively through.
		QStringList extensions;			 //! The list of allowed extensions.
		QFlags<QDir::Filter> dirFilters; //! The directory filter flag.
	};

	//! Create a search job with the given @p parent (if any).
	FileSearchJob(QObject* parent = nullptr);

	//! Stop processing and destroys the object.
	~FileSearchJob();

	FileSearchJob(const FileSearchJob&) = delete;
	FileSearchJob(FileSearchJob&&) = delete;
	FileSearchJob& operator=(const FileSearchJob&) = delete;
	FileSearchJob& operator=(FileSearchJob&&) = delete;

	//! Start @p task and echo @p generation on every signal.
	//! Asks any previous search to stop, then returns without waiting.
	//! A slow directory read cannot be interrupted; it exits after the current entry.
	void search(Task task, quint64 generation);

	//! Ask the active search to stop. Does not block and does not start a new search.
	void cancel();

signals:
	//! Emitted with up to 64 matching paths from @p generation.
	void foundMatches(quint64 generation, const QStringList& paths);

	//! Emitted when @p generation has started searching.
	void started(quint64 generation);

	//! Emitted when @p generation finished without being stopped.
	void finished(quint64 generation);

	//! Emitted when @p generation stopped before a normal finish.
	void cancelled(quint64 generation);

	//! Emitted when @p generation failed. @p message is not translated.
	void failed(quint64 generation, const QString& message);

private:
	struct Request
	{
		quint64 generation = 0;
		std::atomic<bool> stop{false};
	};

	void reapReady();

	std::shared_ptr<Request> m_current;
	std::mutex m_pendingMutex;
	std::vector<std::future<void>> m_pending;
};
} // namespace lmms::gui

#endif // LMMS_GUI_FILE_SEARCH_JOB_H
