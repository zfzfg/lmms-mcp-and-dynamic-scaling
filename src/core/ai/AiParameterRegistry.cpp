#include "AiParameterRegistry.h"
#include "Song.h"
#include "AutomatableModel.h"
#include <algorithm>

namespace lmms::ai
{

AiParameterRegistry* AiParameterRegistry::instance()
{
	static AiParameterRegistry s_instance;
	return &s_instance;
}

void AiParameterRegistry::registerBuiltins(Song* song)
{
	if (!song) { return; }
	QMutexLocker locker(&m_mutex);
	m_bindings.clear();

	// Tempo
	ParameterBinding tempo;
	tempo.parameterId = "prm_song_tempo";
	tempo.ownerId = "song";
	tempo.key = "tempo";
	tempo.displayName = "Song Tempo";
	tempo.model = &song->tempoModel();
	tempo.minValue = MinTempo;
	tempo.maxValue = MaxTempo;
	tempo.step = 1.0;
	tempo.unit = "BPM";
	tempo.isWritable = true;
	m_bindings.append(tempo);
}

void AiParameterRegistry::clear()
{
	QMutexLocker locker(&m_mutex);
	m_bindings.clear();
}

QList<ParameterInfo> AiParameterRegistry::listParameters(const QString& ownerId, const QString& cursor, int limit) const
{
	QMutexLocker locker(&m_mutex);
	QList<ParameterInfo> result;

	int startIndex = 0;
	if (!cursor.isEmpty())
	{
		bool ok = false;
		int parsed = cursor.toInt(&ok);
		if (ok && parsed >= 0)
		{
			startIndex = parsed;
		}
	}

	int count = 0;
	for (int i = startIndex; i < m_bindings.size() && count < limit; ++i)
	{
		const auto& b = m_bindings[i];
		if (!ownerId.isEmpty() && b.ownerId != ownerId)
		{
			continue;
		}

		ParameterInfo info;
		info.parameterId = b.parameterId;
		info.ownerId = b.ownerId;
		info.key = b.key;
		info.displayName = b.displayName;
		info.minValue = b.minValue;
		info.maxValue = b.maxValue;
		info.step = b.step;
		info.unit = b.unit;
		info.isWritable = b.isWritable;

		if (b.model)
		{
			if (auto autoModel = dynamic_cast<AutomatableModel*>(b.model))
			{
				info.value = autoModel->value<double>();
				info.isAutomated = (autoModel->controllerConnection() != nullptr);
			}
		}

		result.append(info);
		count++;
	}

	return result;
}

DomainError AiParameterRegistry::setParameterValue(const QString& parameterId, double value)
{
	QMutexLocker locker(&m_mutex);
	DomainError err;

	for (const auto& b : m_bindings)
	{
		if (b.parameterId == parameterId)
		{
			if (!b.isWritable)
			{
				err.code = ErrorCode::PermissionDenied;
				err.message = QString("Parameter '%1' is read-only.").arg(parameterId);
				return err;
			}

			if (value < b.minValue || value > b.maxValue)
			{
				err.code = ErrorCode::ValidationError;
				err.message = QString("Value %1 out of bounds [%2, %3] for parameter '%4'.")
					.arg(value).arg(b.minValue).arg(b.maxValue).arg(parameterId);
				return err;
			}

			if (auto autoModel = dynamic_cast<AutomatableModel*>(b.model))
			{
				if (autoModel->controllerConnection() != nullptr)
				{
					err.code = ErrorCode::ParameterControlled;
					err.message = QString("Parameter '%1' is controlled by an active automation or controller.").arg(parameterId);
					return err;
				}

				if (auto intModel = dynamic_cast<IntModel*>(b.model))
				{
					intModel->setValue(static_cast<int>(std::round(value)));
				}
				else if (auto floatModel = dynamic_cast<FloatModel*>(b.model))
				{
					floatModel->setValue(static_cast<float>(value));
				}
				return err;
			}
		}
	}

	err.code = ErrorCode::StaleHandle;
	err.message = QString("Unknown parameter ID: %1").arg(parameterId);
	return err;
}

bool AiParameterRegistry::getParameterInfo(const QString& parameterId, ParameterInfo& outInfo) const
{
	QMutexLocker locker(&m_mutex);
	for (const auto& b : m_bindings)
	{
		if (b.parameterId == parameterId)
		{
			outInfo.parameterId = b.parameterId;
			outInfo.ownerId = b.ownerId;
			outInfo.key = b.key;
			outInfo.displayName = b.displayName;
			outInfo.minValue = b.minValue;
			outInfo.maxValue = b.maxValue;
			outInfo.step = b.step;
			outInfo.unit = b.unit;
			outInfo.isWritable = b.isWritable;
			if (auto autoModel = dynamic_cast<AutomatableModel*>(b.model))
			{
				outInfo.value = autoModel->value<double>();
				outInfo.isAutomated = (autoModel->controllerConnection() != nullptr);
			}
			return true;
		}
	}
	return false;
}

} // namespace lmms::ai
