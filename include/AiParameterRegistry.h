#ifndef LMMS_AI_PARAMETER_REGISTRY_H
#define LMMS_AI_PARAMETER_REGISTRY_H

#include <QString>
#include <QList>
#include <QMutex>
#include <memory>
#include "lmms_export.h"
#include "AiDomainTypes.h"

namespace lmms
{
class Model;
class AutomatableModel;
class Song;
}

namespace lmms::ai
{

struct ParameterBinding
{
	QString parameterId;
	QString ownerId;
	QString key;
	QString displayName;
	Model* model{nullptr};
	double minValue{0.0};
	double maxValue{1.0};
	double step{0.01};
	QString unit;
	bool isWritable{true};
};

class LMMS_EXPORT AiParameterRegistry
{
public:
	static AiParameterRegistry* instance();

	void registerBuiltins(Song* song);
	void clear();

	QList<ParameterInfo> listParameters(const QString& ownerId, const QString& cursor, int limit) const;
	DomainError setParameterValue(const QString& parameterId, double value);
	bool getParameterInfo(const QString& parameterId, ParameterInfo& outInfo) const;

private:
	AiParameterRegistry() = default;
	mutable QMutex m_mutex;
	QList<ParameterBinding> m_bindings;
};

} // namespace lmms::ai

#endif // LMMS_AI_PARAMETER_REGISTRY_H
