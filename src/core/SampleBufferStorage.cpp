/*
 * SampleBufferStorage.cpp - immutable sample storage without decoder dependencies
 * Copyright (c) 2026 LMMS Developers
 * This file is part of LMMS - https://lmms.io
 */
#include "SampleBuffer.h"
#include <cstring>
#include <stdexcept>
namespace lmms {
namespace
{

quint64 mixSampleBits(quint64 hash, float value)
{
	uint32_t encoded = 0;
	std::memcpy(&encoded, &value, sizeof(encoded));
	hash ^= encoded;
	hash *= 1099511628211ull;
	return hash;
}

quint64 revisionOf(const std::vector<SampleFrame>& frames)
{
	auto hash = 1469598103934665603ull ^ static_cast<quint64>(frames.size());
	for (const auto& frame : frames)
	{
		hash = mixSampleBits(hash, frame.left());
		hash = mixSampleBits(hash, frame.right());
	}
	return hash;
}

}

SampleBuffer::SampleBuffer(const SampleFrame* data, size_t numFrames, int sampleRate)
    : m_sampleRate(sampleRate)
{
    if (numFrames && !data) { throw std::invalid_argument("Non-empty sample storage requires data"); }
    if (numFrames) { m_data.assign(data, data + numFrames); }
    m_contentRevision = revisionOf(m_data);
}

SampleBuffer::SampleBuffer(std::vector<SampleFrame> data, int sampleRate, const QString& audioFile)
	: m_data(std::move(data))
	, m_audioFile(audioFile)
	, m_sampleRate(sampleRate)
	, m_contentRevision(revisionOf(m_data))
{
}

void swap(SampleBuffer& first, SampleBuffer& second) noexcept
{
	using std::swap;
	swap(first.m_data, second.m_data);
	swap(first.m_audioFile, second.m_audioFile);
	swap(first.m_sampleRate, second.m_sampleRate);
	swap(first.m_contentRevision, second.m_contentRevision);
}

} // namespace lmms
