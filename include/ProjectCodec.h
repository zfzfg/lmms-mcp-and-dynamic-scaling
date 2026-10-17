/*
 * ProjectCodec.h - classify, budget, and decode LMMS project bytes
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_PROJECT_CODEC_H
#define LMMS_PROJECT_CODEC_H

#include <QByteArray>
#include <QString>

namespace lmms
{

struct ProjectCodecLimits
{
	qint64 maxInputBytes = 64LL * 1024 * 1024;
	int maxDecodedBytes = 256 * 1024 * 1024;
	int maxElements = 2000000;
	int maxDepth = 64;
	int maxAttributesPerElement = 256;
	qint64 maxTextBytes = 64LL * 1024 * 1024;
};

enum class ProjectBytesKind
{
	Empty,
	Xml,
	QtCompressed,
	Unrecognized
};

enum class ProjectCodecStatus
{
	Ok,
	InputTooLarge,
	Xml,
	XmlBudget,
	UnsupportedDocument,
	Header,
	Budget,
	Checksum,
	Truncated,
	Trailing,
	NoProgress,
	Unavailable
};

struct ProjectCodecResult
{
	ProjectCodecStatus status = ProjectCodecStatus::Ok;
	QByteArray bytes;
	QString message;
	int line = -1;
	int column = -1;
	//! Peak output bytes retained while inflating. Zero when inflate did not run.
	qint64 peakDecodedBytes = 0;
};

class ProjectCodec
{
public:
	static void setLimits(const ProjectCodecLimits& limits);
	static ProjectCodecLimits limits();

	static ProjectBytesKind classify(const QByteArray& data);
	static bool looksLikeXml(const QByteArray& data);
	static bool looksLikeQtCompressed(const QByteArray& data);

	//! Streaming pre-pass. Rejects extreme depth before a DOM parser sees the text.
	static ProjectCodecResult inspectXml(const QByteArray& xml);

	//! Qt's 4-byte size header plus a zlib wrapper. The declared size is a
	//! candidate: output grows in fixed blocks and stops at the budget.
	static ProjectCodecResult decodeQtCompressed(const QByteArray& compressed);
};

} // namespace lmms

#endif // LMMS_PROJECT_CODEC_H
