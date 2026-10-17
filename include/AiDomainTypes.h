#ifndef LMMS_AI_DOMAIN_TYPES_H
#define LMMS_AI_DOMAIN_TYPES_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdint>
#include <vector>
#include <optional>

#include "LmmsTypes.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms::ai
{

constexpr uint32_t MAX_IPC_FRAME_SIZE = 1024 * 1024; // 1 MiB
constexpr uint32_t MAX_PENDING_COMMANDS = 64;
constexpr uint32_t MAX_BATCH_COMMANDS = 64;
constexpr uint32_t MAX_BATCH_NOTES = 512;
constexpr uint32_t DEFAULT_PAGE_LIMIT = 100;
constexpr uint32_t MAX_PAGE_LIMIT = 500;
constexpr int DEFAULT_DEADLINE_MS = 5000;
constexpr int PLAN_LIFETIME_SECONDS = 300; // 5 minutes

enum class ErrorCode
{
	Ok = 0,
	NotConnected,
	NotReady,
	PermissionDenied,
	WriterBusy,
	Busy,
	ProjectChanged,
	StaleHandle,
	StaleCursor,
	RevisionConflict,
	ValidationError,
	UnsupportedOperation,
	PlaybackActive,
	ParameterControlled,
	PathNotAllowed,
	AssetNotFound,
	PluginUnavailable,
	PluginLoadFailed,
	PlanExpired,
	OperationKeyReused,
	HistoryConflict,
	RenderFailed,
	ExportValidationFailed,
	Timeout,
	Cancelled,
	InternalError
};

LMMS_EXPORT QString errorCodeToString(ErrorCode code);
LMMS_EXPORT ErrorCode stringToErrorCode(const QString& str);

struct LMMS_EXPORT DomainError
{
	ErrorCode code{ErrorCode::Ok};
	QString message;
	bool retryable{false};
	QJsonObject details;

	bool hasError() const { return code != ErrorCode::Ok; }

	QJsonObject toJson() const
	{
		QJsonObject obj;
		obj["code"] = errorCodeToString(code);
		obj["message"] = message;
		obj["retryable"] = retryable;
		if (!details.isEmpty())
		{
			obj["details"] = details;
		}
		return obj;
	}

	static DomainError fromJson(const QJsonObject& obj)
	{
		DomainError err;
		err.code = stringToErrorCode(obj["code"].toString());
		err.message = obj["message"].toString();
		err.retryable = obj["retryable"].toBool(false);
		err.details = obj["details"].toObject();
		return err;
	}
};

enum class Permission : uint32_t
{
	None = 0,
	Read = 1 << 0,
	WriteProject = 1 << 1,
	Assets = 1 << 2,
	Export = 1 << 3,
	ManageFiles = 1 << 4
};

inline Permission operator|(Permission a, Permission b)
{
	return static_cast<Permission>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasPermission(Permission set, Permission check)
{
	return (static_cast<uint32_t>(set) & static_cast<uint32_t>(check)) == static_cast<uint32_t>(check);
}

LMMS_EXPORT QStringList permissionsToStringList(Permission p);
LMMS_EXPORT Permission permissionsFromStringList(const QStringList& list);

struct LMMS_EXPORT NoteData
{
	QString noteRef;
	tick_t positionTicks{0};
	tick_t lengthTicks{48};
	int midiKey{60};
	int volumePercent{100};
	int pan{0};
	QString noteType{"regular"};
	bool hasDetuning{false};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["noteRef"] = noteRef;
		o["positionTicks"] = static_cast<qint64>(positionTicks);
		o["lengthTicks"] = static_cast<qint64>(lengthTicks);
		o["midiKey"] = midiKey;
		o["volumePercent"] = volumePercent;
		o["pan"] = pan;
		o["noteType"] = noteType;
		o["hasDetuning"] = hasDetuning;
		return o;
	}

	static NoteData fromJson(const QJsonObject& o)
	{
		NoteData n;
		n.noteRef = o["noteRef"].toString();
		n.positionTicks = static_cast<tick_t>(o["positionTicks"].toVariant().toLongLong());
		n.lengthTicks = static_cast<tick_t>(o["lengthTicks"].toVariant().toLongLong());
		n.midiKey = o["midiKey"].toInt(60);
		n.volumePercent = o["volumePercent"].toInt(100);
		n.pan = o["pan"].toInt(0);
		n.noteType = o["noteType"].toString("regular");
		n.hasDetuning = o["hasDetuning"].toBool(false);
		return n;
	}
};

struct LMMS_EXPORT TrackSummary
{
	QString trackId;
	QString name;
	QString type;
	QString container; // "song" or "pattern_store"
	bool muted{false};
	bool solo{false};
	float volume{1.0f};
	float panning{0.0f};
	int clipCount{0};
	QString instrumentKey;
	int mixerChannel{0};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["trackId"] = trackId;
		o["name"] = name;
		o["type"] = type;
		o["container"] = container;
		o["muted"] = muted;
		o["solo"] = solo;
		o["volume"] = volume;
		o["panning"] = panning;
		o["clipCount"] = clipCount;
		if (!instrumentKey.isEmpty()) { o["instrumentKey"] = instrumentKey; }
		o["mixerChannel"] = mixerChannel;
		return o;
	}
};

struct LMMS_EXPORT ClipSummary
{
	QString clipId;
	QString trackId;
	QString type;
	QString name;
	tick_t positionTicks{0};
	tick_t lengthTicks{0};
	bool muted{false};
	int noteCount{0};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["clipId"] = clipId;
		o["trackId"] = trackId;
		o["type"] = type;
		o["name"] = name;
		o["positionTicks"] = static_cast<qint64>(positionTicks);
		o["lengthTicks"] = static_cast<qint64>(lengthTicks);
		o["muted"] = muted;
		o["noteCount"] = noteCount;
		return o;
	}
};

struct LMMS_EXPORT ProjectSummary
{
	QString projectId;
	uint64_t projectRevision{0};
	QString name;
	bool modified{false};
	int tempoBpm{140};
	int timeSigNumerator{4};
	int timeSigDenominator{4};
	int ticksPerQuarter{48};
	int ticksPerBar{192};
	tick_t lengthTicks{0};
	QString playMode{"song"};
	bool isPlaying{false};
	bool isPaused{false};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["projectId"] = projectId;
		o["projectRevision"] = static_cast<qint64>(projectRevision);
		o["name"] = name;
		o["modified"] = modified;
		o["tempoBpm"] = tempoBpm;
		QJsonObject ts;
		ts["numerator"] = timeSigNumerator;
		ts["denominator"] = timeSigDenominator;
		o["timeSignature"] = ts;
		o["ticksPerQuarter"] = ticksPerQuarter;
		o["ticksPerBar"] = ticksPerBar;
		o["lengthTicks"] = static_cast<qint64>(lengthTicks);
		o["playMode"] = playMode;
		o["isPlaying"] = isPlaying;
		o["isPaused"] = isPaused;
		return o;
	}
};

struct LMMS_EXPORT ParameterInfo
{
	QString parameterId;
	QString ownerId;
	QString key;
	QString displayName;
	double value{0.0};
	double minValue{0.0};
	double maxValue{1.0};
	double step{0.01};
	QString unit;
	bool isAutomated{false};
	bool isWritable{true};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["parameterId"] = parameterId;
		o["ownerId"] = ownerId;
		o["key"] = key;
		o["displayName"] = displayName;
		o["value"] = value;
		o["minValue"] = minValue;
		o["maxValue"] = maxValue;
		o["step"] = step;
		o["unit"] = unit;
		o["isAutomated"] = isAutomated;
		o["isWritable"] = isWritable;
		return o;
	}
};

struct LMMS_EXPORT MixerSendInfo
{
	int targetChannelIndex{0};
	float amount{1.0f};

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["targetChannelIndex"] = targetChannelIndex;
		o["amount"] = amount;
		return o;
	}
};

struct LMMS_EXPORT MixerChannelSummary
{
	int channelIndex{0};
	QString name;
	bool muted{false};
	bool solo{false};
	float volume{1.0f};
	bool isMaster{false};
	std::vector<MixerSendInfo> sends;

	QJsonObject toJson() const
	{
		QJsonObject o;
		o["channelIndex"] = channelIndex;
		o["name"] = name;
		o["muted"] = muted;
		o["solo"] = solo;
		o["volume"] = volume;
		o["isMaster"] = isMaster;
		QJsonArray sendsArr;
		for (const auto& s : sends)
		{
			sendsArr.append(s.toJson());
		}
		o["sends"] = sendsArr;
		return o;
	}
};

} // namespace lmms::ai

#endif // LMMS_AI_DOMAIN_TYPES_H
