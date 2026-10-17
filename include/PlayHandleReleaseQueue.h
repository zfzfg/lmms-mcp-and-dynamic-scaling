/*
 * PlayHandleReleaseQueue.h - fixed handoff for pointers leaving the audio thread
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_PLAY_HANDLE_RELEASE_QUEUE_H
#define LMMS_PLAY_HANDLE_RELEASE_QUEUE_H

#include <atomic>
#include <cstddef>

namespace lmms
{

//! Single-producer, single-consumer pointer queue. push fails when full.
//! The caller keeps the pointer; this queue never deletes and never drops.
template<typename T, std::size_t Capacity>
class BoundedPointerQueue
{
	static_assert(Capacity > 0, "release queue capacity must be positive");

public:
	bool tryPush(T* pointer) noexcept
	{
		const auto write = m_write.load(std::memory_order_relaxed);
		const auto next = (write + 1) % Capacity;
		if (next == m_read.load(std::memory_order_acquire)) { return false; }
		m_slots[write] = pointer;
		m_write.store(next, std::memory_order_release);
		return true;
	}

	T* tryPop() noexcept
	{
		const auto read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire)) { return nullptr; }
		T* pointer = m_slots[read];
		m_slots[read] = nullptr;
		m_read.store((read + 1) % Capacity, std::memory_order_release);
		return pointer;
	}

	std::size_t sizeApprox() const noexcept
	{
		const auto write = m_write.load(std::memory_order_acquire);
		const auto read = m_read.load(std::memory_order_acquire);
		return write >= read ? write - read : Capacity - (read - write);
	}

private:
	T* m_slots[Capacity]{};
	std::atomic<std::size_t> m_write{0};
	std::atomic<std::size_t> m_read{0};
};

class PlayHandle;
using PlayHandleReleaseQueue = BoundedPointerQueue<PlayHandle, 1024>;

} // namespace lmms

#endif // LMMS_PLAY_HANDLE_RELEASE_QUEUE_H
