/*
 * AudioDisplayBuffer.h - triple-buffer snapshot of one audio period for GUI meters
 *
 * Copyright (c) 2026 LMMS Developers
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

#ifndef LMMS_AUDIO_DISPLAY_BUFFER_H
#define LMMS_AUDIO_DISPLAY_BUFFER_H

#include <atomic>
#include <cstdint>
#include <cstring>

#include "LmmsTypes.h"
#include "SampleFrame.h"

namespace lmms
{

//! Single-writer snapshot of the latest audio period.
//! The audio thread writes a slot the GUI is not copying, then publishes that
//! slot index. The GUI copies only a published slot and may drop frames.
//! A failed copy leaves the caller's buffer unchanged.
class AudioDisplayBuffer
{
public:
	// Matches AudioEngine::MAXIMUM_BUFFER_SIZE. Kept literal to avoid including AudioEngine.h.
	static constexpr f_cnt_t kMaxFrames = 4096;
	static constexpr int kSlots = 3;

	void publish(const SampleFrame* source, f_cnt_t frames) noexcept
	{
		if (source == nullptr || frames <= 0) { return; }
		const auto count = frames > kMaxFrames ? kMaxFrames : frames;
		const int slot = pickWriteSlot();
		std::memcpy(m_slots[slot].frames, source, sizeof(SampleFrame) * static_cast<size_t>(count));
		m_slots[slot].frameCount = count;
		m_published.store(slot, std::memory_order_release);
		m_writing = (slot + 1) % kSlots;
	}

	//! Copies the latest published snapshot. Returns false when nothing has
	//! been published yet or the published index changed before the copy was claimed.
	//! Does not write @p destination on failure.
	bool copyLatest(SampleFrame* destination, f_cnt_t capacity, f_cnt_t& outFrames) const noexcept
	{
		if (destination == nullptr || capacity <= 0) { return false; }
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const int index = m_published.load(std::memory_order_acquire);
			if (index < 0 || index >= kSlots) { return false; }
			m_reading.store(index, std::memory_order_release);
			if (m_published.load(std::memory_order_acquire) != index)
			{
				m_reading.store(-1, std::memory_order_release);
				continue;
			}
			const f_cnt_t available = m_slots[index].frameCount;
			const f_cnt_t count = available > capacity ? capacity : available;
			if (count > 0)
			{
				std::memcpy(destination, m_slots[index].frames, sizeof(SampleFrame) * static_cast<size_t>(count));
			}
			m_reading.store(-1, std::memory_order_release);
			outFrames = count;
			return count > 0;
		}
		m_reading.store(-1, std::memory_order_release);
		return false;
	}

private:
	struct Slot
	{
		f_cnt_t frameCount = 0;
		SampleFrame frames[kMaxFrames]{};
	};

	int pickWriteSlot() const noexcept
	{
		const int published = m_published.load(std::memory_order_acquire);
		const int reading = m_reading.load(std::memory_order_acquire);
		if (m_writing != published && m_writing != reading) { return m_writing; }
		for (int slot = 0; slot < kSlots; ++slot)
		{
			if (slot != published && slot != reading) { return slot; }
		}
		// The reader holds one slot and one slot is published. A third remains.
		return (published + 1) % kSlots;
	}

	Slot m_slots[kSlots]{};
	mutable std::atomic<int> m_published{-1};
	mutable std::atomic<int> m_reading{-1};
	int m_writing = 0;
};

} // namespace lmms

#endif // LMMS_AUDIO_DISPLAY_BUFFER_H
