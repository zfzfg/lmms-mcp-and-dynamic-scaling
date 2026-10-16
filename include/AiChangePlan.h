#ifndef LMMS_AI_CHANGE_PLAN_H
#define LMMS_AI_CHANGE_PLAN_H

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <memory>

#include "AiDomainTypes.h"
#include "lmms_export.h"

namespace lmms::ai {

struct LMMS_EXPORT ChangeCommand
{
	QString kind;
	QJsonObject rawParams;

	// Typed fields
	int bpm{0};
	QString trackId;
	QString newName;
	bool muted{false};
	QString parameterId;
	double paramValue{0.0};
	QString clientRef;
	tick_t positionTicks{0};
	tick_t lengthTicks{0};
	QString targetClipId;
	QString targetClipClientRef;
	std::vector<NoteData> notes;
	QStringList noteRefs;
	int transposeSemitones{0};
	int patternIndex{0};
	int stepIndex{0};
	bool stepEnabled{false};
	int mixerChannelIndex{0};
	int mixerSendTargetIndex{0};
	float mixerSendAmount{1.0f};

	static ChangeCommand fromJson(const QJsonObject& obj, DomainError& outErr);
	QJsonObject toJson() const;
};

class LMMS_EXPORT AiChangePlan
{
public:
	AiChangePlan(const QString& planId, const QString& projectId, uint64_t expectedRevision,
		const std::vector<ChangeCommand>& commands);

	QString planId() const { return m_planId; }
	QString projectId() const { return m_projectId; }
	uint64_t expectedRevision() const { return m_expectedRevision; }
	QString planHash() const { return m_planHash; }
	QDateTime expiresAt() const { return m_expiresAt; }
	bool isExpired() const { return QDateTime::currentDateTimeUtc() > m_expiresAt; }

	const std::vector<ChangeCommand>& commands() const { return m_commands; }
	QString summary() const { return m_summary; }
	QStringList affectedObjects() const { return m_affectedObjects; }

	QJsonObject toJson() const;

private:
	void computeHashAndSummary();

	QString m_planId;
	QString m_projectId;
	uint64_t m_expectedRevision{0};
	QString m_planHash;
	QDateTime m_expiresAt;
	std::vector<ChangeCommand> m_commands;
	QString m_summary;
	QStringList m_affectedObjects;
};

class LMMS_EXPORT AiChangePlanManager
{
public:
	static AiChangePlanManager* instance();

	QString storePlan(std::shared_ptr<AiChangePlan> plan);
	std::shared_ptr<AiChangePlan> lookupPlan(const QString& planId);
	void removePlan(const QString& planId);
	void clearExpired();
	void clear();

private:
	AiChangePlanManager() = default;
	mutable QMutex m_mutex;
	QHash<QString, std::shared_ptr<AiChangePlan>> m_plans;
};

} // namespace lmms::ai

#endif // LMMS_AI_CHANGE_PLAN_H
