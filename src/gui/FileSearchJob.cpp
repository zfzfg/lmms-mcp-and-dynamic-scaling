/*
 * FileSearchJob.cpp
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

#include "FileSearchJob.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include <QCoreApplication>
#include <QDirIterator>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>

#include "ThreadPool.h"

namespace lmms::gui {

namespace {
constexpr int kMatchBatchSize = 64;

template<typename Fn>
void postToJob(const QPointer<FileSearchJob>& job, quint64 generation, Fn&& deliver)
{
	auto* app = QCoreApplication::instance();
	if (app == nullptr) { return; }
	QMetaObject::invokeMethod(
		app,
		[job, generation, deliver = std::forward<Fn>(deliver)]() {
			if (!job) { return; }
			deliver(job.data(), generation);
		},
		Qt::QueuedConnection);
}

void runSearch(FileSearchJob::Task task, quint64 generation, const std::shared_ptr<std::atomic<bool>>& stopFlag, const QPointer<FileSearchJob>& target)
{
	const auto stopped = [stopFlag] { return stopFlag->load(std::memory_order_acquire); };

	try
	{
		// RE expression that matches regular tokens, and tokens with double quotes around them
		static auto s_tokenRe = QRegularExpression{R"(\"([^"]+)\"|(\S+))"};

		auto tokensIt = s_tokenRe.globalMatch(task.filter);
		auto tokens = QStringList{};

		while (tokensIt.hasNext())
		{
			const auto match = tokensIt.next();
			const auto quoted = match.captured(1);
			const auto plain = match.captured(2);

			if (!quoted.isEmpty()) { tokens.push_back(quoted); }
			if (!plain.isEmpty()) { tokens.push_back(plain); }
		}

		if (stopped())
		{
			postToJob(target, generation, [](FileSearchJob* job, quint64 id) { emit job->cancelled(id); });
			return;
		}

		postToJob(target, generation, [](FileSearchJob* job, quint64 id) { emit job->started(id); });

		auto batch = QStringList{};
		batch.reserve(kMatchBatchSize);

		const auto flush = [&] {
			if (batch.isEmpty() || stopped())
			{
				batch.clear();
				return;
			}
			const QStringList paths = batch;
			batch.clear();
			postToJob(target, generation, [paths](FileSearchJob* job, quint64 id) { emit job->foundMatches(id, paths); });
		};

		for (const auto& path : task.paths)
		{
			if (stopped()) { break; }

			// next() can block in the OS. The stop flag is observed between entries only.
			auto dirIt = QDirIterator{path, task.dirFilters,
				QDirIterator::IteratorFlag::Subdirectories | QDirIterator::IteratorFlag::FollowSymlinks};

			while (dirIt.hasNext() && !stopped())
			{
				const auto fileInfo = QFileInfo{dirIt.next()};
				if (stopped()) { break; }

				const auto fileName = fileInfo.fileName();
				const auto containsToken = std::all_of(tokens.begin(), tokens.end(),
					[&](const auto& token) { return fileName.contains(token, Qt::CaseInsensitive); });

				const auto validDir = fileInfo.isDir() && containsToken;
				const auto validFile = fileInfo.isFile() && containsToken
					&& task.extensions.contains(QString{"*.%1"}.arg(fileInfo.completeSuffix()), Qt::CaseInsensitive);

				if (validDir || validFile)
				{
					batch.append(fileInfo.filePath());
					if (batch.size() >= kMatchBatchSize) { flush(); }
				}
			}
		}

		if (stopped())
		{
			batch.clear();
			postToJob(target, generation, [](FileSearchJob* job, quint64 id) { emit job->cancelled(id); });
			return;
		}

		flush();
		postToJob(target, generation, [](FileSearchJob* job, quint64 id) { emit job->finished(id); });
	}
	catch (const std::exception& ex)
	{
		const auto message = QString::fromUtf8(ex.what());
		postToJob(target, generation, [message](FileSearchJob* job, quint64 id) { emit job->failed(id, message); });
	}
	catch (...)
	{
		postToJob(target, generation, [](FileSearchJob* job, quint64 id) { emit job->failed(id, QStringLiteral("Search failed")); });
	}
}

}

FileSearchJob::FileSearchJob(QObject* parent)
	: QObject(parent)
{
}

FileSearchJob::~FileSearchJob()
{
	// Do not join. A stuck directory read must not freeze the GUI thread.
	// The worker posts through QPointer and drops results after this object is gone.
	cancel();
	const auto lock = std::lock_guard{m_pendingMutex};
	m_pending.clear();
}

void FileSearchJob::cancel()
{
	if (m_current) { m_current->stop.store(true, std::memory_order_release); }
}

void FileSearchJob::search(Task task, quint64 generation)
{
	cancel();

	auto request = std::make_shared<Request>();
	request->generation = generation;
	m_current = request;

	// Distinguishes "pool rejected the task" from "task already finished".
	auto accepted = std::make_shared<std::atomic<bool>>(false);

	try
	{
		QPointer<FileSearchJob> self{this};
		auto future = ThreadPool::instance().enqueue([self, task = std::move(task), request, accepted]() mutable {
			accepted->store(true, std::memory_order_release);
			try
			{
				if (self) { self->reapReady(); }
				auto stopFlag = std::shared_ptr<std::atomic<bool>>(request, &request->stop);
				runSearch(std::move(task), request->generation, stopFlag, self);
			}
			catch (...)
			{
				postToJob(self, request->generation, [](FileSearchJob* job, quint64 generation) {
					emit job->failed(generation, QStringLiteral("Search failed"));
				});
			}
		});

		const auto rejected = future.wait_for(std::chrono::seconds(0)) == std::future_status::ready
			&& !accepted->load(std::memory_order_acquire);

		{
			const auto lock = std::lock_guard{m_pendingMutex};
			m_pending.emplace_back(std::move(future));
		}

		if (rejected) { emit failed(generation, QStringLiteral("Search could not be started")); }
	}
	catch (const std::exception& ex)
	{
		emit failed(generation, QString::fromUtf8(ex.what()));
	}
	catch (...)
	{
		emit failed(generation, QStringLiteral("Search could not be started"));
	}
}

void FileSearchJob::reapReady()
{
	auto ready = std::vector<std::future<void>>{};
	{
		const auto lock = std::lock_guard{m_pendingMutex};
		auto stillPending = std::vector<std::future<void>>{};
		stillPending.reserve(m_pending.size());
		for (auto& future : m_pending)
		{
			if (!future.valid()) { continue; }
			if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				ready.emplace_back(std::move(future));
			}
			else
			{
				stillPending.emplace_back(std::move(future));
			}
		}
		m_pending.swap(stillPending);
	}

	for (auto& future : ready)
	{
		try
		{
			future.get();
		}
		catch (...)
		{
		}
	}
}

} // namespace lmms::gui
