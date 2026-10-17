/*
 * SampleThumbnail.cpp
 *
 * Copyright (c) 2024 Khoi Dau <casboi86@gmail.com>
 * Copyright (c) 2024 Sotonye Atemie <sakertooth@gmail.com>
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

#include "SampleThumbnail.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QFileInfo>
#include <QPainter>

#include "Sample.h"

namespace {
	constexpr auto AggregationPerZoomStep = 10;
}

namespace lmms::gui {

SampleThumbnail::Thumbnail::Thumbnail(std::vector<Peak> peaks, double samplesPerPeak)
	: m_peaks(std::move(peaks))
	, m_samplesPerPeak(samplesPerPeak)
{
}

SampleThumbnail::Thumbnail::Thumbnail(const float* buffer, size_t size, size_t width)
	: m_peaks(width)
	, m_samplesPerPeak(width == 0 ? 1.0 : std::max(static_cast<double>(size) / width, 1.0))
{
	if (buffer == nullptr || size == 0 || width == 0) { return; }
	for (auto peakIndex = std::size_t{0}; peakIndex < width; ++peakIndex)
	{
		auto begin = static_cast<size_t>(std::floor(peakIndex * m_samplesPerPeak));
		auto end = static_cast<size_t>(std::ceil((peakIndex + 1) * m_samplesPerPeak));
		if (begin >= size) { break; }
		if (end <= begin) { end = begin + 1; }
		if (end > size) { end = size; }
		const auto [min, max] = std::minmax_element(buffer + begin, buffer + end);
		m_peaks[peakIndex] = Peak{*min, *max};
	}
}

SampleThumbnail::Thumbnail SampleThumbnail::Thumbnail::zoomOut(float factor) const
{
	assert(factor >= 1 && "Invalid zoom out factor");

	auto peaks = std::vector<Peak>(m_peaks.size() / factor);
	for (auto peakIndex = std::size_t{0}; peakIndex < peaks.size(); ++peakIndex)
	{
		auto beginIndex = static_cast<size_t>(std::floor(peakIndex * factor));
		auto endIndex = static_cast<size_t>(std::ceil((peakIndex + 1) * factor));
		if (beginIndex >= m_peaks.size()) { break; }
		if (endIndex <= beginIndex) { endIndex = beginIndex + 1; }
		if (endIndex > m_peaks.size()) { endIndex = m_peaks.size(); }
		const auto beginAggregationAt = m_peaks.begin() + static_cast<std::ptrdiff_t>(beginIndex);
		const auto endAggregationAt = m_peaks.begin() + static_cast<std::ptrdiff_t>(endIndex);
		peaks[peakIndex] = std::accumulate(beginAggregationAt, endAggregationAt, Peak{});
	}

	return Thumbnail{std::move(peaks), m_samplesPerPeak * factor};
}

SampleThumbnail::SampleThumbnail(const Sample& sample)
	: m_buffer(sample.buffer())
{
	const auto fileInfo = QFileInfo{sample.sampleFile()};
	auto entry = SampleThumbnailEntry{sample.sampleFile(), fileInfo.lastModified(), fileInfo.size(), contentRevisionOf(*m_buffer)};
	if (!entry.filePath.isEmpty())
	{
		const auto it = s_sampleThumbnailCacheMap.find(entry);
		if (it != s_sampleThumbnailCacheMap.end())
		{
			it->second.lastUse = ++s_cacheTick;
			m_thumbnailCache = it->second.cache;
			return;
		}

		auto cachedBytes = std::size_t{0};
		for (const auto& record : s_sampleThumbnailCacheMap) { cachedBytes += record.second.bytes; }
		while (!s_sampleThumbnailCacheMap.empty()
			&& (s_sampleThumbnailCacheMap.size() >= kMaxCacheEntries || cachedBytes > kMaxCacheBytes))
		{
			auto victim = s_sampleThumbnailCacheMap.end();
			for (auto candidate = s_sampleThumbnailCacheMap.begin(); candidate != s_sampleThumbnailCacheMap.end(); ++candidate)
			{
				const bool onlyCached = candidate->second.cache.use_count() == 1;
				if (victim == s_sampleThumbnailCacheMap.end()
					|| (onlyCached && victim->second.cache.use_count() != 1)
					|| (onlyCached == (victim->second.cache.use_count() == 1) && candidate->second.lastUse < victim->second.lastUse))
				{
					victim = candidate;
				}
			}
			cachedBytes -= victim->second.bytes;
			s_sampleThumbnailCacheMap.erase(victim);
		}

		s_sampleThumbnailCacheMap.emplace(std::move(entry), CacheRecord{m_thumbnailCache, 0, ++s_cacheTick});
	}

	// Float count is frame count times channel count. Empty storage has no pointer to dereference.
	const float* flatBuffer = nullptr;
	auto flatBufferSize = std::size_t{0};
	if (!m_buffer->empty() && m_buffer->data() != nullptr)
	{
		flatBuffer = m_buffer->data()->data();
		flatBufferSize = m_buffer->size() * DEFAULT_CHANNELS;
	}
	m_thumbnailCache->emplace_back(flatBuffer, flatBufferSize, flatBufferSize / AggregationPerZoomStep);

	while (m_thumbnailCache->back().width() >= AggregationPerZoomStep)
	{
		auto zoomedOutThumbnail = m_thumbnailCache->back().zoomOut(AggregationPerZoomStep);
		m_thumbnailCache->emplace_back(std::move(zoomedOutThumbnail));
	}

	auto bytes = std::size_t{0};
	for (const auto& level : *m_thumbnailCache)
	{
		bytes += static_cast<std::size_t>(std::max(level.width(), 0)) * sizeof(float) * 2;
	}
	for (auto& record : s_sampleThumbnailCacheMap)
	{
		if (record.second.cache == m_thumbnailCache) { record.second.bytes = bytes; }
	}
}

void SampleThumbnail::visualize(VisualizeParameters parameters, QPainter& painter) const
{
	const auto& sampleRect = parameters.sampleRect;
	const auto& viewportRect = parameters.viewportRect.isNull() ? sampleRect : parameters.viewportRect;

	const auto renderRect = sampleRect.intersected(viewportRect);
	if (renderRect.isNull()) { return; }

	if (!std::isfinite(parameters.sampleStart) || !std::isfinite(parameters.sampleEnd)) { return; }
	const auto sampleRange = parameters.sampleEnd - parameters.sampleStart;
	if (sampleRange <= 0.0f || sampleRange > 1.0f) { return; }
	if (m_buffer->empty() || m_buffer->data() == nullptr || sampleRect.width() <= 0) { return; }

	const auto targetThumbnailWidth = static_cast<int>(sampleRect.width() / sampleRange);
	if (targetThumbnailWidth <= 0) { return; }

	const auto finerThumbnail = std::find_if(m_thumbnailCache->rbegin(), m_thumbnailCache->rend(),
		[&](const auto& thumbnail) { return thumbnail.width() >= targetThumbnailWidth; });

	const auto useFrames = finerThumbnail == m_thumbnailCache->rend();
	const auto frameCount = m_buffer->size();
	const auto sourceWidth = useFrames ? frameCount : static_cast<size_t>(finerThumbnail->width());
	if (sourceWidth == 0) { return; }

	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);

	const auto thumbnailBeginForward = std::max<int>(renderRect.x() - sampleRect.x(), parameters.sampleStart * targetThumbnailWidth);
	const auto thumbnailEndForward = std::max<int>(renderRect.x() + renderRect.width() - sampleRect.x(), parameters.sampleEnd * targetThumbnailWidth);
	const auto thumbnailBegin = parameters.reversed ? targetThumbnailWidth - thumbnailBeginForward - 1 : thumbnailBeginForward;
	const auto thumbnailEnd = parameters.reversed ? targetThumbnailWidth - thumbnailEndForward : thumbnailEndForward;
	const auto advanceThumbnailBy = parameters.reversed ? -1 : 1;
	const auto yScale = renderRect.height() / 2 * parameters.amplification;

	for (auto x = renderRect.x(), i = thumbnailBegin; x < renderRect.x() + renderRect.width() && i != thumbnailEnd;
		++x, i += advanceThumbnailBy)
	{
		if (i < 0) { continue; }
		const auto span = columnSpan(sourceWidth, i, targetThumbnailWidth);
		if (span.empty()) { continue; }

		auto minPeak = 0.f;
		auto maxPeak = 0.f;
		if (useFrames)
		{
			const auto peak = peakOverFrames(m_buffer->data(), frameCount, span.begin, span.end);
			minPeak = peak.first;
			maxPeak = peak.second;
		}
		else
		{
			const auto beginAggregationAt = finerThumbnail->data() + span.begin;
			const auto endAggregationAt = finerThumbnail->data() + span.end;
			const auto peak = std::accumulate(beginAggregationAt, endAggregationAt, Thumbnail::Peak{});
			minPeak = peak.min;
			maxPeak = peak.max;
		}

		const auto yMin = renderRect.center().y() - minPeak * yScale;
		const auto yMax = renderRect.center().y() - maxPeak * yScale;
		painter.drawLine(x, yMin, x, yMax);
	}

	painter.restore();
}

auto SampleThumbnail::columnSpan(size_t sourceWidth, int column, int targetWidth) -> ColumnSpan
{
	ColumnSpan span;
	if (sourceWidth == 0 || targetWidth <= 0 || column < 0) { return span; }
	const auto scale = static_cast<double>(sourceWidth) / static_cast<double>(targetWidth);
	const auto begin = static_cast<size_t>(std::floor(static_cast<double>(column) * scale));
	auto end = static_cast<size_t>(std::ceil((static_cast<double>(column) + 1.0) * scale));
	if (begin >= sourceWidth) { return span; }
	if (end <= begin) { end = begin + 1; }
	if (end > sourceWidth) { end = sourceWidth; }
	span.begin = begin;
	span.end = end;
	return span;
}

auto SampleThumbnail::contentRevisionOf(const SampleBuffer& buffer) -> quint64
{
	return buffer.contentRevision();
}

auto SampleThumbnail::chooseSource(size_t frameCount, size_t finestPeakWidth, int targetWidth) -> SourceChoice
{
	if (frameCount == 0 || targetWidth <= 0) { return {}; }
	if (finestPeakWidth >= static_cast<size_t>(targetWidth) && finestPeakWidth > 0)
	{
		return {WaveformSource::Peaks, finestPeakWidth};
	}
	return {WaveformSource::Frames, frameCount};
}

auto SampleThumbnail::peakOverFrames(const SampleFrame* frames, size_t frameCount, size_t begin, size_t end) -> std::pair<float, float>
{
	if (frames == nullptr || begin >= end || begin >= frameCount) { return {0.f, 0.f}; }
	if (end > frameCount) { end = frameCount; }
	auto minPeak = std::numeric_limits<float>::infinity();
	auto maxPeak = -std::numeric_limits<float>::infinity();
	for (auto index = begin; index < end; ++index)
	{
		minPeak = std::min(minPeak, std::min(frames[index].left(), frames[index].right()));
		maxPeak = std::max(maxPeak, std::max(frames[index].left(), frames[index].right()));
	}
	if (!std::isfinite(minPeak) || !std::isfinite(maxPeak)) { return {0.f, 0.f}; }
	return {minPeak, maxPeak};
}

auto SampleThumbnail::cacheBytes() -> std::size_t
{
	auto bytes = std::size_t{0};
	for (const auto& record : s_sampleThumbnailCacheMap) { bytes += record.second.bytes; }
	return bytes;
}

void SampleThumbnail::clearCache()
{
	s_sampleThumbnailCacheMap.clear();
}

} // namespace lmms::gui
