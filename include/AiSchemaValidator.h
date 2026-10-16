#ifndef LMMS_AI_SCHEMA_VALIDATOR_H
#define LMMS_AI_SCHEMA_VALIDATOR_H
#include "AiDomainTypes.h"
namespace lmms::ai {
LMMS_EXPORT DomainError validateToolArguments(const QString& action, const QJsonObject& arguments);
LMMS_EXPORT DomainError validateCommandSchema(const QJsonObject& command);
} // namespace lmms::ai
#endif
