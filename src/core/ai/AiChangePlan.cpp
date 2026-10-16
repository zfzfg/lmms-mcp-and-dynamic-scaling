#include "AiChangePlan.h"

#include <QCryptographicHash>
#include <QUuid>
#include <cmath>
#include <limits>

#include "AiSchemaValidator.h"

namespace lmms::ai {

ChangeCommand ChangeCommand::fromJson(const QJsonObject& obj, DomainError& outErr)
{
	ChangeCommand cmd;
	outErr = validateCommandSchema(obj);
	if (outErr.hasError()) { return cmd; }
	cmd.rawParams = obj;
	cmd.kind = obj["kind"].toString();

	auto fail = [&](const QString& message) { outErr = {ErrorCode::ValidationError, message}; };
	auto integer = [](const QJsonValue& value, double low, double high) {
		return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() == std::floor(value.toDouble())
			&& value.toDouble() >= low && value.toDouble() <= high;
	};
	const double maxTick = std::numeric_limits<tick_t>::max();
	QStringList required;
	if (cmd.kind == "set_tempo") { required << "bpm"; }
	if (cmd.kind == "create_midi_clip") { required << "positionTicks" << "lengthTicks"; }
	if (cmd.kind == "transpose_notes") { required << "semitones"; }
	if (cmd.kind == "set_mixer_send")
	{
		required << "amount";
		if (!obj.contains("fromChannelId") || !obj.contains("toChannelId"))
		{
			required << "fromChannelIndex" << "toChannelIndex";
		}
	}
	if (cmd.kind == "set_parameter") { required << "value"; }
	for (const auto& key : required)
	{
		auto value = obj.value(key);
		if (!value.isDouble() || !std::isfinite(value.toDouble()))
		{
			fail("Missing or invalid number: " + key);
			return cmd;
		}
		if (key != "amount" && key != "value" && !integer(value, key == "semitones" ? -127 : 0, maxTick))
		{
			fail("Invalid integer: " + key);
			return cmd;
		}
	}
	if (obj.contains("positionTicks") && obj.contains("lengthTicks")
		&& obj["positionTicks"].toDouble() + obj["lengthTicks"].toDouble() > maxTick)
	{
		fail("Clip end overflows native tick range.");
		return cmd;
	}
	if (cmd.kind == "set_track_mute" && !obj["muted"].isBool())
	{
		fail("muted must be a boolean.");
		return cmd;
	}
	if (cmd.kind == "add_notes" || cmd.kind == "update_notes")
	{
		if (!obj["notes"].isArray() || obj["notes"].toArray().isEmpty()
			|| obj["notes"].toArray().size() > static_cast<int>(MAX_BATCH_NOTES))
		{
			fail("Expected 1..512 notes.");
			return cmd;
		}
		for (const auto& value : obj["notes"].toArray())
		{
			if (!value.isObject())
			{
				fail("Note must be an object.");
				return cmd;
			}
			const auto note = value.toObject();
			if (!integer(note["positionTicks"], 0, maxTick) || !integer(note["lengthTicks"], 1, maxTick)
				|| !integer(note["midiKey"], 0, 127)
				|| note["positionTicks"].toDouble() + note["lengthTicks"].toDouble() > maxTick
				|| (note.contains("volumePercent") && !integer(note["volumePercent"], 0, 200))
				|| (note.contains("pan") && !integer(note["pan"], -100, 100)))
			{
				fail("Invalid note numbers or native tick overflow.");
				return cmd;
			}
		}
	}
	if (cmd.kind == "remove_notes" && (!obj["noteRefs"].isArray() || obj["noteRefs"].toArray().isEmpty()))
	{
		fail("Expected nonempty noteRefs.");
		return cmd;
	}

	if (cmd.kind.isEmpty())
	{
		outErr.code = ErrorCode::ValidationError;
		outErr.message = "Command missing 'kind' property.";
		return cmd;
	}

	if (cmd.kind == "set_tempo")
	{
		cmd.bpm = obj["bpm"].toInt(0);
		if (cmd.bpm < 10 || cmd.bpm > 999)
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = QString("BPM out of valid range 10..999: %1").arg(cmd.bpm);
			return cmd;
		}
	}
	else if (cmd.kind == "rename_track")
	{
		cmd.trackId = obj["trackId"].toString();
		cmd.newName = obj["newName"].toString();
		if (cmd.trackId.isEmpty() || cmd.newName.isEmpty())
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "rename_track requires 'trackId' and non-empty 'newName'.";
			return cmd;
		}
	}
	else if (cmd.kind == "set_track_mute")
	{
		cmd.trackId = obj["trackId"].toString();
		cmd.muted = obj["muted"].toBool(false);
		if (cmd.trackId.isEmpty())
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "set_track_mute requires 'trackId'.";
			return cmd;
		}
	}
	else if (cmd.kind == "set_parameter")
	{
		cmd.parameterId = obj["parameterId"].toString();
		cmd.paramValue = obj["value"].toDouble();
		if (cmd.parameterId.isEmpty())
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "set_parameter requires 'parameterId'.";
			return cmd;
		}
	}
	else if (cmd.kind == "create_midi_clip")
	{
		cmd.trackId = obj["trackId"].toString();
		cmd.clientRef = obj["clientRef"].toString();
		cmd.positionTicks = static_cast<tick_t>(obj["positionTicks"].toVariant().toLongLong());
		cmd.lengthTicks = static_cast<tick_t>(obj["lengthTicks"].toVariant().toLongLong());
		if (cmd.trackId.isEmpty() || cmd.clientRef.isEmpty() || cmd.lengthTicks <= 0)
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "create_midi_clip requires valid 'trackId', 'clientRef', and 'lengthTicks' > 0.";
			return cmd;
		}
	}
	else if (cmd.kind == "add_notes")
	{
		QJsonObject clipObj = obj["clip"].toObject();
		cmd.targetClipId = clipObj["clipId"].toString();
		cmd.targetClipClientRef = clipObj["clientRef"].toString();
		if (cmd.targetClipId.isEmpty() && cmd.targetClipClientRef.isEmpty())
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "add_notes requires either 'clipId' or 'clientRef' in 'clip' object.";
			return cmd;
		}
		QJsonArray notesArr = obj["notes"].toArray();
		if (notesArr.isEmpty())
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "add_notes requires non-empty 'notes' array.";
			return cmd;
		}
		for (const auto& val : notesArr)
		{
			NoteData note = NoteData::fromJson(val.toObject());
			if (note.midiKey < 0 || note.midiKey > 127)
			{
				outErr.code = ErrorCode::ValidationError;
				outErr.message = QString("Note midiKey out of range 0..127: %1").arg(note.midiKey);
				return cmd;
			}
			if (note.lengthTicks <= 0)
			{
				outErr.code = ErrorCode::ValidationError;
				outErr.message = "Note lengthTicks must be > 0.";
				return cmd;
			}
			cmd.notes.push_back(note);
		}
	}
	else if (cmd.kind == "update_notes")
	{
		cmd.targetClipId = obj["clipId"].toString();
		QJsonArray notesArr = obj["notes"].toArray();
		for (const auto& val : notesArr)
		{
			NoteData note = NoteData::fromJson(val.toObject());
			if (note.noteRef.isEmpty())
			{
				outErr.code = ErrorCode::ValidationError;
				outErr.message = "update_notes requires valid 'noteRef' on each note.";
				return cmd;
			}
			cmd.notes.push_back(note);
		}
	}
	else if (cmd.kind == "remove_notes")
	{
		cmd.targetClipId = obj["clipId"].toString();
		QJsonArray refs = obj["noteRefs"].toArray();
		for (const auto& val : refs)
		{
			cmd.noteRefs.append(val.toString());
		}
	}
	else if (cmd.kind == "transpose_notes")
	{
		cmd.targetClipId = obj["clipId"].toString();
		cmd.transposeSemitones = obj["semitones"].toInt(0);
		for (const auto& ref : obj["noteRefs"].toArray())
		{
			cmd.noteRefs.append(ref.toString());
		}
		if (cmd.targetClipId.isEmpty() || cmd.transposeSemitones == 0)
		{
			outErr.code = ErrorCode::ValidationError;
			outErr.message = "transpose_notes requires 'clipId' and non-zero 'semitones'.";
			return cmd;
		}
	}
	else if (cmd.kind == "create_mixer_channel") { cmd.clientRef = obj["clientRef"].toString(); }
	else if (cmd.kind == "set_mixer_send")
	{
		cmd.mixerChannelIndex = obj["fromChannelIndex"].toInt(0);
		cmd.mixerSendTargetIndex = obj["toChannelIndex"].toInt(0);
		cmd.mixerSendAmount = static_cast<float>(obj["amount"].toDouble(1.0));
	}

	else if (cmd.kind == "move_clip" || cmd.kind == "resize_clip" || cmd.kind == "duplicate_clip"
		|| cmd.kind == "delete_clip" || cmd.kind == "set_pattern_steps" || cmd.kind == "set_automation_points")
	{
		cmd.targetClipId = obj["clipId"].toString();
		cmd.parameterId = obj["parameterId"].toString();
		cmd.clientRef = obj["clientRef"].toString();
		if (cmd.targetClipId.isEmpty() && cmd.kind != "set_automation_points")
		{
			fail("clipId required.");
			return cmd;
		}
		if (cmd.kind == "set_automation_points" && cmd.targetClipId.isEmpty())
		{
			if (cmd.clientRef.isEmpty() || !integer(obj["positionTicks"], 0, maxTick)
				|| !integer(obj["lengthTicks"], 1, maxTick))
			{
				fail("New automation requires clientRef, positionTicks and lengthTicks.");
				return cmd;
			}
			cmd.positionTicks = static_cast<tick_t>(obj["positionTicks"].toDouble());
			cmd.lengthTicks = static_cast<tick_t>(obj["lengthTicks"].toDouble());
		}
		if (cmd.kind == "move_clip" || cmd.kind == "duplicate_clip")
		{
			if (!integer(obj["positionTicks"], 0, maxTick))
			{
				fail("Invalid clip position.");
				return cmd;
			}
			cmd.positionTicks = static_cast<tick_t>(obj["positionTicks"].toDouble());
		}
		if (cmd.kind == "resize_clip")
		{
			if (!integer(obj["lengthTicks"], 1, maxTick))
			{
				fail("Invalid clip length.");
				return cmd;
			}
			cmd.lengthTicks = static_cast<tick_t>(obj["lengthTicks"].toDouble());
		}
	}
	else if (cmd.kind == "delete_track" || cmd.kind == "assign_mixer_channel")
	{
		cmd.trackId = obj["trackId"].toString();
		if (cmd.trackId.isEmpty())
		{
			fail("trackId required.");
			return cmd;
		}
	}
	else if (cmd.kind == "assign_pattern")
	{
		cmd.trackId = obj["patternId"].toString();
		cmd.clientRef = obj["clientRef"].toString();
		if (cmd.trackId.isEmpty() || !integer(obj["positionTicks"], 0, maxTick)
			|| !integer(obj["lengthTicks"], 1, maxTick)
			|| obj["positionTicks"].toDouble() + obj["lengthTicks"].toDouble() > maxTick)
		{
			fail("assign_pattern requires patternId, positionTicks and lengthTicks.");
			return cmd;
		}
		cmd.positionTicks = static_cast<tick_t>(obj["positionTicks"].toDouble());
		cmd.lengthTicks = static_cast<tick_t>(obj["lengthTicks"].toDouble());
	}
	else
	{
		outErr.code = ErrorCode::UnsupportedOperation;
		outErr.message = QString("Unsupported change command kind: %1").arg(cmd.kind);
		return cmd;
	}

	return cmd;
}

QJsonObject ChangeCommand::toJson() const
{
	return rawParams;
}

AiChangePlan::AiChangePlan(const QString& planId, const QString& projectId, uint64_t expectedRevision,
	const std::vector<ChangeCommand>& commands)
	: m_planId(planId)
	, m_projectId(projectId)
	, m_expectedRevision(expectedRevision)
	, m_expiresAt(QDateTime::currentDateTimeUtc().addSecs(PLAN_LIFETIME_SECONDS))
	, m_commands(commands)
{
	computeHashAndSummary();
}

void AiChangePlan::computeHashAndSummary()
{
	QJsonArray arr;
	QStringList summaries;

	for (const auto& cmd : m_commands)
	{
		arr.append(cmd.toJson());

		if (cmd.kind == "set_tempo") { summaries.append(QString("Set tempo to %1 BPM").arg(cmd.bpm)); }
		else if (cmd.kind == "rename_track")
		{
			summaries.append(QString("Rename track %1 to '%2'").arg(cmd.trackId, cmd.newName));
			m_affectedObjects.append(cmd.trackId);
		}
		else if (cmd.kind == "set_track_mute")
		{
			summaries.append(QString("Set mute on track %1 to %2").arg(cmd.trackId).arg(cmd.muted ? "true" : "false"));
			m_affectedObjects.append(cmd.trackId);
		}
		else if (cmd.kind == "set_parameter")
		{
			summaries.append(QString("Set parameter %1 to %2").arg(cmd.parameterId).arg(cmd.paramValue));
			m_affectedObjects.append(cmd.parameterId);
		}
		else if (cmd.kind == "create_midi_clip")
		{
			summaries.append(QString("Create MIDI clip on track %1 at tick %2 (len: %3)")
					.arg(cmd.trackId)
					.arg(static_cast<qint64>(cmd.positionTicks))
					.arg(static_cast<qint64>(cmd.lengthTicks)));
			m_affectedObjects.append(cmd.trackId);
		}
		else if (cmd.kind == "add_notes")
		{
			summaries.append(QString("Add %1 notes to clip").arg(cmd.notes.size()));
			if (!cmd.targetClipId.isEmpty()) { m_affectedObjects.append(cmd.targetClipId); }
		}
		else if (cmd.kind == "update_notes")
		{
			summaries.append(QString("Update %1 notes in clip %2").arg(cmd.notes.size()).arg(cmd.targetClipId));
			m_affectedObjects.append(cmd.targetClipId);
		}
		else if (cmd.kind == "remove_notes")
		{
			summaries.append(QString("Remove %1 notes from clip %2").arg(cmd.noteRefs.size()).arg(cmd.targetClipId));
			m_affectedObjects.append(cmd.targetClipId);
		}
		else if (cmd.kind == "transpose_notes")
		{
			summaries.append(QString("Transpose notes in clip %1 by %2 semitones")
					.arg(cmd.targetClipId)
					.arg(cmd.transposeSemitones));
			m_affectedObjects.append(cmd.targetClipId);
		}
		else if (cmd.kind == "create_mixer_channel") { summaries.append("Create new mixer channel"); }
		else if (cmd.kind == "set_mixer_send")
		{
			summaries.append(QString("Set mixer send from ch %1 to ch %2 (%3)")
					.arg(cmd.mixerChannelIndex)
					.arg(cmd.mixerSendTargetIndex)
					.arg(cmd.mixerSendAmount));
		}
		else
		{
			summaries.append(QString("%1: track=%2 clip=%3 parameter=%4")
					.arg(cmd.kind, cmd.trackId, cmd.targetClipId, cmd.parameterId));
			if (cmd.kind == "set_automation_points" && cmd.targetClipId.isEmpty())
			{
				summaries.append(QString("Create automation track and clip at %1, length %2, %3 points")
						.arg(cmd.positionTicks)
						.arg(cmd.lengthTicks)
						.arg(cmd.rawParams["points"].toArray().size()));
			}
			if (!cmd.trackId.isEmpty()) { m_affectedObjects.append(cmd.trackId); }
			if (!cmd.targetClipId.isEmpty()) { m_affectedObjects.append(cmd.targetClipId); }
			if (!cmd.parameterId.isEmpty()) { m_affectedObjects.append(cmd.parameterId); }
		}
	}

	m_summary = summaries.join("; ");

	QByteArray serialized = QJsonDocument(arr).toJson(QJsonDocument::Compact);
	m_planHash = QString::fromLatin1(QCryptographicHash::hash(serialized, QCryptographicHash::Sha256).toHex());
}

QJsonObject AiChangePlan::toJson() const
{
	QJsonObject o;
	o["planId"] = m_planId;
	o["projectId"] = m_projectId;
	o["expectedRevision"] = static_cast<qint64>(m_expectedRevision);
	o["planHash"] = m_planHash;
	o["expiresAt"] = m_expiresAt.toString(Qt::ISODate);
	o["summary"] = m_summary;
	o["operationsCount"] = static_cast<int>(m_commands.size());

	QJsonArray aff;
	for (const auto& a : m_affectedObjects)
	{
		aff.append(a);
	}
	o["affectedObjects"] = aff;
	return o;
}

AiChangePlanManager* AiChangePlanManager::instance()
{
	static AiChangePlanManager s_instance;
	return &s_instance;
}

void AiChangePlanManager::clear()
{
	QMutexLocker locker(&m_mutex);
	m_plans.clear();
}

QString AiChangePlanManager::storePlan(std::shared_ptr<AiChangePlan> plan)
{
	if (!plan) { return QString(); }
	QMutexLocker locker(&m_mutex);
	clearExpired();
	if (m_plans.size() >= 128) { return {}; }
	m_plans.insert(plan->planId(), plan);
	return plan->planId();
}

std::shared_ptr<AiChangePlan> AiChangePlanManager::lookupPlan(const QString& planId)
{
	QMutexLocker locker(&m_mutex);
	clearExpired();
	if (!m_plans.contains(planId)) { return nullptr; }
	return m_plans.value(planId);
}

void AiChangePlanManager::removePlan(const QString& planId)
{
	QMutexLocker locker(&m_mutex);
	m_plans.remove(planId);
}

void AiChangePlanManager::clearExpired()
{
	QDateTime now = QDateTime::currentDateTimeUtc();
	auto it = m_plans.begin();
	while (it != m_plans.end())
	{
		if (it.value()->expiresAt() < now) { it = m_plans.erase(it); }
		else { ++it; }
	}
}

} // namespace lmms::ai
