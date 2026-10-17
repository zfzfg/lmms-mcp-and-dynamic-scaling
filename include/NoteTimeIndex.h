/*
 * NoteTimeIndex.h - time-overlap index for piano-roll note candidates
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_NOTE_TIME_INDEX_H
#define LMMS_NOTE_TIME_INDEX_H

#include <algorithm>
#include <limits>
#include <vector>

namespace lmms
{

//! Half-open in time only at the caller's discretion. Overlap is closed:
//! start <= rangeEnd && end >= rangeStart. Spans are sorted by start, so a
//! note that begins before the viewport and extends into it is still found.
class NoteTimeIndex
{
public:
	struct Span
	{
		int start = 0;
		int end = 0;
		int noteIndex = 0;
	};

	static constexpr int kMinNotes = 256;

	//! Negative note lengths are drawn as 4 ticks. A detune curve uses the
	//! last automation node when that extends past the note body.
	static int drawnTicks(int lengthTicks, int detuneLastKey)
	{
		const int len = lengthTicks < 0 ? 4 : lengthTicks;
		const int detune = detuneLastKey < 0 ? 0 : detuneLastKey;
		return std::max(len, detune);
	}

	//! Inclusive drawn range. A negative detune key, which loadSettings can
	//! store even though the editor clamps new nodes to 0, pulls the start
	//! left. The end covers the note body and the rightmost detune key.
	static void spanBounds(int noteStart, int lengthTicks, int detuneFirstKey, int detuneLastKey,
		int extra, int* spanStart, int* spanEnd)
	{
		const long long length = lengthTicks < 0 ? 4LL : static_cast<long long>(lengthTicks);
		const long long left = std::min(0LL, static_cast<long long>(detuneFirstKey));
		const long long right = std::max(length, std::max(0LL, static_cast<long long>(detuneLastKey)));
		const long long start = static_cast<long long>(noteStart) + left;
		const long long end = static_cast<long long>(noteStart) + right + static_cast<long long>(extra);
		auto clampToInt = [](long long value) {
			if (value > std::numeric_limits<int>::max()) { return std::numeric_limits<int>::max(); }
			if (value < std::numeric_limits<int>::min()) { return std::numeric_limits<int>::min(); }
			return static_cast<int>(value);
		};
		*spanStart = clampToInt(start);
		*spanEnd = clampToInt(end);
	}

	void clear()
	{
		m_spans.clear();
		m_maxEnd.clear();
	}

	auto size() const -> int { return static_cast<int>(m_spans.size()); }

	void rebuild(const std::vector<Span>& spans)
	{
		m_spans = spans;
		std::sort(m_spans.begin(), m_spans.end(), [](const Span& a, const Span& b) {
			if (a.start != b.start) { return a.start < b.start; }
			return a.noteIndex < b.noteIndex;
		});

		const int n = static_cast<int>(m_spans.size());
		m_maxEnd.assign(static_cast<std::size_t>(std::max(n, 1)) * 4, std::numeric_limits<int>::min());
		if (n == 0) { return; }

		struct Builder
		{
			const std::vector<Span>& spans;
			std::vector<int>& maxEnd;
			void build(int node, int lo, int hi) const
			{
				if (lo == hi)
				{
					maxEnd[static_cast<std::size_t>(node)] = spans[static_cast<std::size_t>(lo)].end;
					return;
				}
				const int mid = lo + (hi - lo) / 2;
				build(node * 2, lo, mid);
				build(node * 2 + 1, mid + 1, hi);
				maxEnd[static_cast<std::size_t>(node)] = std::max(
					maxEnd[static_cast<std::size_t>(node * 2)], maxEnd[static_cast<std::size_t>(node * 2 + 1)]);
			}
		};
		Builder{m_spans, m_maxEnd}.build(1, 0, n - 1);
	}

	void query(int rangeStart, int rangeEnd, std::vector<int>& out) const
	{
		out.clear();
		if (rangeStart > rangeEnd) { std::swap(rangeStart, rangeEnd); }
		const int n = static_cast<int>(m_spans.size());
		if (n == 0 || m_maxEnd.empty()) { return; }

		const auto pastEnd = std::upper_bound(m_spans.begin(), m_spans.end(), rangeEnd,
			[](int value, const Span& span) { return value < span.start; });
		const int right = static_cast<int>(pastEnd - m_spans.begin());
		if (right <= 0) { return; }

		struct Walker
		{
			const std::vector<Span>& spans;
			const std::vector<int>& maxEnd;
			int right;
			int rangeStart;
			std::vector<int>& out;

			void walk(int node, int lo, int hi) const
			{
				if (lo >= right || node < 0 || node >= static_cast<int>(maxEnd.size())) { return; }
				if (maxEnd[static_cast<std::size_t>(node)] < rangeStart) { return; }
				if (lo == hi)
				{
					if (spans[static_cast<std::size_t>(lo)].end >= rangeStart)
					{
						out.push_back(spans[static_cast<std::size_t>(lo)].noteIndex);
					}
					return;
				}
				const int mid = lo + (hi - lo) / 2;
				walk(node * 2, lo, mid);
				walk(node * 2 + 1, mid + 1, hi);
			}
		};
		Walker{m_spans, m_maxEnd, right, rangeStart, out}.walk(1, 0, n - 1);
	}

private:
	std::vector<Span> m_spans;
	std::vector<int> m_maxEnd;
};

} // namespace lmms

#endif // LMMS_NOTE_TIME_INDEX_H
