#ifndef LMMS_AI_WAV_FILE_H
#define LMMS_AI_WAV_FILE_H
#include "AiDomainTypes.h"
namespace lmms::ai {
struct WavInfo
{
	int channels{0};
	int sampleRate{0};
	int bitDepth{0};
	qint64 dataBytes{0};
	double durationSeconds{0};
};
LMMS_EXPORT DomainError inspectWavFile(const QString& path, WavInfo& info, int expectedRate = 0, int expectedDepth = 0);
} // namespace lmms::ai
#endif
