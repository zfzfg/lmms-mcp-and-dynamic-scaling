#include "AiWavFile.h"

#include <QFile>
#include <QtEndian>
namespace lmms::ai {
DomainError inspectWavFile(const QString& filePath, WavInfo& info, int expectedRate, int expectedDepth)
{
	QString outError;

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		outError = "Could not open output file for reading.";
		return {ErrorCode::ExportValidationFailed, outError};
	}

	if (file.size() < 44 || file.size() > 2LL * 1024 * 1024 * 1024)
	{
		outError = QString("File too small to be a valid WAV (%1 bytes)").arg(file.size());
		return {ErrorCode::ExportValidationFailed, outError};
	}

	const auto header = file.read(12);
	if (header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE"
		|| static_cast<quint64>(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 4))) + 8
			!= static_cast<quint64>(file.size()))
	{
		outError = "Invalid RIFF size/signature.";
		return {ErrorCode::ExportValidationFailed, outError};
	}
	bool format = false, data = false;
	quint16 channels = 0, bits = 0, align = 0;
	quint32 rate = 0;
	while (file.pos() + 8 <= file.size())
	{
		auto chunk = file.read(8);
		quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunk.constData() + 4));
		if (static_cast<quint64>(file.pos()) + size > static_cast<quint64>(file.size()))
		{
			outError = "Truncated WAV chunk.";
			return {ErrorCode::ExportValidationFailed, outError};
		}
		if (chunk.left(4) == "fmt ")
		{
			if (size < 16 || size > 65536)
			{
				outError = "Invalid fmt chunk.";
				return {ErrorCode::ExportValidationFailed, outError};
			}
			const auto fmt = file.read(size);
			auto u16 = [&](int offset) {
				return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(fmt.constData() + offset));
			};
			auto u32 = [&](int offset) {
				return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(fmt.constData() + offset));
			};
			channels = u16(2);
			rate = u32(4);
			align = u16(12);
			bits = u16(14);
			const auto subtype = fmt.mid(24, 16);
			const bool extensible = u16(0) == 0xfffe && size >= 40 && u16(18) == bits
				&& (subtype == QByteArray::fromHex("0100000000001000800000aa00389b71")
					|| subtype == QByteArray::fromHex("0300000000001000800000aa00389b71"));
			format = (u16(0) == 1 || (u16(0) == 3 && bits == 32) || extensible) && channels > 0 && channels <= 2
				&& rate >= 8000 && rate <= 192000 && (bits == 16 || bits == 24 || bits == 32)
				&& align == channels * bits / 8 && u32(8) == rate * align;
		}
		else
		{
			if (chunk.left(4) == "data")
			{
				data = format && size > 0 && size % align == 0;
				info.dataBytes = size;
			}
			file.seek(file.pos() + size);
		}
		if (size & 1) { file.seek(file.pos() + 1); }
	}
	if (!format || !data)
	{
		outError = "WAV has no valid format or audio frames.";
		return {ErrorCode::ExportValidationFailed, outError};
	}

	if ((expectedRate && rate != expectedRate) || (expectedDepth && bits != expectedDepth))
	{
		return {ErrorCode::ExportValidationFailed, "Rendered WAV does not match requested sample rate/bit depth."};
	}
	info.channels = channels;
	info.sampleRate = rate;
	info.bitDepth = bits;
	info.durationSeconds = static_cast<double>(info.dataBytes) / (rate * align);
	if (info.durationSeconds <= 0 || info.durationSeconds > 3600)
	{
		return {ErrorCode::ExportValidationFailed, "WAV duration exceeds supported range (0..3600 seconds)."};
	}
	return {};
}
} // namespace lmms::ai
