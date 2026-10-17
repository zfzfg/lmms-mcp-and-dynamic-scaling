/*
 * ThreadPool.h
 *
 * Copyright (c) 2024 saker
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

#ifndef LMMS_THREAD_POOL_H
#define LMMS_THREAD_POOL_H

#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace lmms {
//! A thread pool that can be used for asynchronous processing.
class ThreadPool
{
public:
	//! Thrown into a future when enqueue is rejected because the pool is stopping.
	class Stopped : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	//! Thrown into a future when the queue is already at its limit.
	class QueueFull : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	static constexpr size_t kMinWorkers = 1;
	static constexpr size_t kMaxWorkers = 64;
	static constexpr size_t kDefaultQueueLimit = 4096;

	//! @p numWorkers below 1 selects the minimum. Shutdown runs tasks already queued
	//! and rejects new ones with @ref Stopped. Each accepted task sets its promise once.
	explicit ThreadPool(size_t numWorkers, size_t maxQueued = kDefaultQueueLimit);

	//! Destroys the `ThreadPool` object.
	//! This blocks until all workers have finished executing queued tasks.
	~ThreadPool();

	//! Enqueue function `fn` with arguments `args` to be ran asynchronously.
	template <typename Fn, typename... Args>
	auto enqueue(Fn&& fn, Args&&... args) -> std::future<std::invoke_result_t<Fn, Args...>>
	{
		using ReturnType = std::invoke_result_t<Fn, Args...>;

		auto promise = std::make_shared<std::promise<ReturnType>>();
		auto task = [promise, fn = std::forward<Fn>(fn), args = std::make_tuple(std::forward<Args>(args)...)]() mutable
		{
			try
			{
				PromiseFulfill<ReturnType>::apply(*promise, fn, args);
			}
			catch (...)
			{
				try
				{
					promise->set_exception(std::current_exception());
				}
				catch (...)
				{
				}
			}
		};

		{
			const auto lock = std::unique_lock{m_runMutex};
			if (!m_accepting)
			{
				promise->set_exception(std::make_exception_ptr(Stopped{"ThreadPool is not accepting work"}));
				return promise->get_future();
			}
			if (m_queue.size() >= m_maxQueued)
			{
				promise->set_exception(std::make_exception_ptr(QueueFull{"ThreadPool queue is full"}));
				return promise->get_future();
			}
			m_queue.push(std::move(task));
		}

		m_runCond.notify_one();
		return promise->get_future();
	}

	//! Return the number of worker threads used.
	auto numWorkers() const -> size_t;

	//! Workers to create when the CPU count is unavailable or zero.
	static auto defaultWorkerCount() -> size_t;

	//! Return the global `ThreadPool` instance.
	static auto instance() -> ThreadPool&;

private:
	template<typename T>
	struct PromiseFulfill
	{
		template<typename Fn, typename Tuple>
		static void apply(std::promise<T>& promise, Fn& fn, Tuple& args)
		{
			promise.set_value(std::apply(fn, args));
		}
	};

private:
	void run();
	std::vector<std::thread> m_workers;
	std::queue<std::function<void()>> m_queue;
	std::atomic<bool> m_done = false;
	bool m_accepting = true;
	size_t m_maxQueued = kDefaultQueueLimit;
	std::condition_variable m_runCond;
	std::mutex m_runMutex;
};

template<>
struct ThreadPool::PromiseFulfill<void>
{
	template<typename Fn, typename Tuple>
	static void apply(std::promise<void>& promise, Fn& fn, Tuple& args)
	{
		std::apply(fn, args);
		promise.set_value();
	}
};

} // namespace lmms

#endif // LMMS_THREAD_POOL_H
