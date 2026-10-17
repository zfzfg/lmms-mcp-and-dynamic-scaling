/*
 * ProjectCodec.cpp - bounded inflate and an XML budget pre-pass
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "ProjectCodec.h"

#include <cstring>
#include <limits>

#include <QXmlStreamReader>
#include <QtEndian>

#if defined(LMMS_HAVE_ZLIB)
#include <zlib.h>
#elif defined(__has_include)
#if __has_include(<QtZlib/zlib.h>)
#include <QtZlib/zlib.h>
#define LMMS_HAVE_ZLIB 1
#endif
#endif

namespace lmms
{

namespace
{

ProjectCodecLimits g_limits;

ProjectCodecResult makeResult(ProjectCodecStatus status, QString message, int line = -1, int column = -1)
{
	ProjectCodecResult result;
	result.status = status;
	result.message = std::move(message);
	result.line = line;
	result.column = column;
	return result;
}

bool hasBom(const QByteArray& data, int* bomBytes)
{
	const auto size = data.size();
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData());
	if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
	{
		*bomBytes = 3;
		return true;
	}
	if (size >= 2 && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF)))
	{
		*bomBytes = 2;
		return true;
	}
	*bomBytes = 0;
	return false;
}

bool containsDoctype(const QByteArray& data)
{
	const QByteArray upper = data.toUpper();
	return upper.contains("<!DOCTYPE") || upper.contains("<!ENTITY");
}

} // namespace

void ProjectCodec::setLimits(const ProjectCodecLimits& limits)
{
	g_limits = limits;
}

ProjectCodecLimits ProjectCodec::limits()
{
	return g_limits;
}

bool ProjectCodec::looksLikeXml(const QByteArray& data)
{
	int bom = 0;
	hasBom(data, &bom);
	for (int index = bom; index < data.size(); ++index)
	{
		const unsigned char ch = static_cast<unsigned char>(data.at(index));
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { continue; }
		return ch == '<';
	}
	return false;
}

bool ProjectCodec::looksLikeQtCompressed(const QByteArray& data)
{
	if (data.size() < 6 || looksLikeXml(data)) { return false; }
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData());
	const unsigned cmf = bytes[4];
	const unsigned flg = bytes[5];
	if ((cmf & 0x0Fu) != 8u) { return false; }
	if ((cmf >> 4) > 7u) { return false; }
	if (((cmf << 8) + flg) % 31u != 0u) { return false; }
	return true;
}

ProjectBytesKind ProjectCodec::classify(const QByteArray& data)
{
	if (data.isEmpty()) { return ProjectBytesKind::Empty; }
	if (looksLikeXml(data)) { return ProjectBytesKind::Xml; }
	if (looksLikeQtCompressed(data)) { return ProjectBytesKind::QtCompressed; }
	return ProjectBytesKind::Unrecognized;
}

ProjectCodecResult ProjectCodec::inspectXml(const QByteArray& xml)
{
	if (static_cast<qint64>(xml.size()) > g_limits.maxInputBytes && static_cast<qint64>(xml.size()) > g_limits.maxDecodedBytes)
	{
		return makeResult(ProjectCodecStatus::InputTooLarge,
			QStringLiteral("XML text exceeds the input budget."));
	}
	if (containsDoctype(xml))
	{
		return makeResult(ProjectCodecStatus::UnsupportedDocument,
			QStringLiteral("Project XML must not declare a DOCTYPE or entity."));
	}

	QXmlStreamReader reader(xml);
	int depth = 0;
	int elements = 0;
	qint64 textBytes = 0;
	while (!reader.atEnd())
	{
		const auto token = reader.readNext();
		if (reader.hasError())
		{
			return makeResult(ProjectCodecStatus::Xml, reader.errorString(),
				static_cast<int>(reader.lineNumber()), static_cast<int>(reader.columnNumber()));
		}
		if (token == QXmlStreamReader::StartElement)
		{
			++elements;
			++depth;
			if (depth > g_limits.maxDepth)
			{
				return makeResult(ProjectCodecStatus::XmlBudget,
					QStringLiteral("Project XML exceeds the depth limit of %1.").arg(g_limits.maxDepth),
					static_cast<int>(reader.lineNumber()), static_cast<int>(reader.columnNumber()));
			}
			if (elements > g_limits.maxElements)
			{
				return makeResult(ProjectCodecStatus::XmlBudget,
					QStringLiteral("Project XML exceeds the element limit of %1.").arg(g_limits.maxElements),
					static_cast<int>(reader.lineNumber()), static_cast<int>(reader.columnNumber()));
			}
			if (reader.attributes().size() > g_limits.maxAttributesPerElement)
			{
				return makeResult(ProjectCodecStatus::XmlBudget,
					QStringLiteral("An XML element exceeds the attribute limit of %1.").arg(g_limits.maxAttributesPerElement),
					static_cast<int>(reader.lineNumber()), static_cast<int>(reader.columnNumber()));
			}
		}
		else if (token == QXmlStreamReader::EndElement)
		{
			if (depth > 0) { --depth; }
		}
		else if (token == QXmlStreamReader::Characters)
		{
			textBytes += reader.text().size();
			if (textBytes > g_limits.maxTextBytes)
			{
				return makeResult(ProjectCodecStatus::XmlBudget,
					QStringLiteral("Project XML exceeds the text budget."),
					static_cast<int>(reader.lineNumber()), static_cast<int>(reader.columnNumber()));
			}
		}
	}
	ProjectCodecResult result;
	result.status = ProjectCodecStatus::Ok;
	result.bytes = xml;
	return result;
}

ProjectCodecResult ProjectCodec::decodeQtCompressed(const QByteArray& compressed)
{
	if (static_cast<qint64>(compressed.size()) > g_limits.maxInputBytes)
	{
		return makeResult(ProjectCodecStatus::InputTooLarge,
			QStringLiteral("Compressed project data exceeds the %1 byte input limit.").arg(g_limits.maxInputBytes));
	}
	if (compressed.size() < 6)
	{
		return makeResult(ProjectCodecStatus::Truncated, QStringLiteral("Compressed project data is incomplete."));
	}
	const auto declared = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(compressed.constData()));
	if (declared > static_cast<quint32>(std::min(g_limits.maxDecodedBytes, std::numeric_limits<int>::max())))
	{
		return makeResult(ProjectCodecStatus::Budget,
			QStringLiteral("Declared uncompressed size %1 exceeds the %2 byte limit. The project was not unpacked.")
				.arg(declared)
				.arg(g_limits.maxDecodedBytes));
	}
	const auto* bytes = reinterpret_cast<const unsigned char*>(compressed.constData());
	const unsigned cmf = bytes[4];
	const unsigned flg = bytes[5];
	if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u || ((cmf << 8) + flg) % 31u != 0u)
	{
		return makeResult(ProjectCodecStatus::Header, QStringLiteral("Compressed project data does not have a zlib header."));
	}
	if ((flg & 0x20u) != 0u)
	{
		return makeResult(ProjectCodecStatus::Header, QStringLiteral("Compressed project data uses a preset dictionary, which is not supported."));
	}

#ifndef LMMS_HAVE_ZLIB
	return makeResult(ProjectCodecStatus::Unavailable,
		QStringLiteral("zlib is not linked, so compressed projects are rejected instead of unpacked without a budget."));
#else
	const qint64 cap = std::min<qint64>(static_cast<qint64>(declared), g_limits.maxDecodedBytes);
	z_stream stream{};
	stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(bytes + 4));
	stream.avail_in = static_cast<uInt>(compressed.size() - 4);
	if (inflateInit(&stream) != Z_OK)
	{
		return makeResult(ProjectCodecStatus::Header, QStringLiteral("Could not start inflate for the compressed project."));
	}

	QByteArray decoded;
	decoded.reserve(static_cast<int>(std::min<qint64>(cap, 1024 * 1024)));
	constexpr int kChunk = 64 * 1024;
	int inflateStatus = Z_OK;
	bool stalled = false;
	while (inflateStatus != Z_STREAM_END)
	{
		if (static_cast<qint64>(decoded.size()) >= cap)
		{
			inflateEnd(&stream);
			return makeResult(ProjectCodecStatus::Budget,
				QStringLiteral("Uncompressed project data exceeded the declared size or the decode budget during unpack."));
		}
		const qint64 room = std::min<qint64>(kChunk, cap - decoded.size());
		const int oldSize = decoded.size();
		decoded.resize(oldSize + static_cast<int>(room));
		stream.next_out = reinterpret_cast<Bytef*>(decoded.data() + oldSize);
		stream.avail_out = static_cast<uInt>(room);
		const uInt beforeIn = stream.avail_in;
		const uInt beforeOut = stream.avail_out;
		inflateStatus = inflate(&stream, Z_NO_FLUSH);
		const bool progressed = stream.avail_in != beforeIn || stream.avail_out != beforeOut;
		decoded.resize(decoded.size() - static_cast<int>(stream.avail_out));
		if (inflateStatus == Z_STREAM_END) { break; }
		if (inflateStatus == Z_DATA_ERROR)
		{
			inflateEnd(&stream);
			return makeResult(ProjectCodecStatus::Checksum,
				QStringLiteral("Compressed project data failed its checksum or is corrupt."));
		}
		if (inflateStatus != Z_OK)
		{
			inflateEnd(&stream);
			return makeResult(ProjectCodecStatus::Truncated,
				QStringLiteral("Compressed project data ended before the zlib stream finished."));
		}
		if (!progressed)
		{
			stalled = true;
			break;
		}
	}
	const auto totalIn = static_cast<int>(stream.total_in);
	inflateEnd(&stream);
	if (stalled)
	{
		return makeResult(ProjectCodecStatus::NoProgress, QStringLiteral("Decompression made no progress and was stopped."));
	}
	if (inflateStatus != Z_STREAM_END)
	{
		return makeResult(ProjectCodecStatus::Truncated, QStringLiteral("Compressed project data ended before the zlib stream finished."));
	}
	if (totalIn != compressed.size() - 4)
	{
		return makeResult(ProjectCodecStatus::Trailing,
			QStringLiteral("Compressed project data has bytes after the zlib stream."));
	}
	if (static_cast<quint32>(decoded.size()) != declared)
	{
		return makeResult(ProjectCodecStatus::Checksum,
			QStringLiteral("Uncompressed size %1 does not match the declared size %2.")
				.arg(decoded.size())
				.arg(declared));
	}
	ProjectCodecResult result;
	result.status = ProjectCodecStatus::Ok;
	result.bytes = std::move(decoded);
	result.peakDecodedBytes = result.bytes.size();
	return result;
#endif
}

} // namespace lmms
