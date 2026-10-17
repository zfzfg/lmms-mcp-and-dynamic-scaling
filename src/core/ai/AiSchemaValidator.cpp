#include "AiSchemaValidator.h"

#include <cmath>

#include "mcp_command_schema.h"
namespace lmms::ai {
namespace {
bool valid(const QJsonValue& value, const QJsonObject& schema, int depth = 0)
{
	if (depth > 32) { return false; }
	if (schema.contains("const") && value != schema["const"]) { return false; }
	if (schema.contains("enum") && !schema["enum"].toArray().contains(value)) { return false; }
	if (schema.contains("not") && valid(value, schema["not"].toObject(), depth + 1)) { return false; }
	if (schema.contains("oneOf"))
	{
		int matches = 0;
		for (const auto& option : schema["oneOf"].toArray())
		{
			matches += valid(value, option.toObject(), depth + 1);
		}
		if (matches != 1) { return false; }
	}
	const auto type = schema["type"].toString();
	if (type == "object")
	{
		if (!value.isObject()) { return false; }
		const auto object = value.toObject(), properties = schema["properties"].toObject();
		for (const auto& key : schema["required"].toArray())
		{
			if (!object.contains(key.toString())) { return false; }
		}
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			if (properties.contains(it.key()))
			{
				if (!valid(it.value(), properties[it.key()].toObject(), depth + 1)) { return false; }
			}
			else if (schema.contains("additionalProperties") && !schema["additionalProperties"].toBool(true))
			{
				return false;
			}
		}
	}
	else if (type == "array")
	{
		if (!value.isArray()) { return false; }
		auto array = value.toArray();
		if (array.size() < schema["minItems"].toInt(0) || array.size() > schema["maxItems"].toInt(2048))
		{
			return false;
		}
		for (int i = 0; i < array.size(); ++i)
		{
			if (!valid(array[i], schema["items"].toObject(), depth + 1)) { return false; }
			if (schema["uniqueItems"].toBool())
			{
				for (int j = 0; j < i; ++j)
				{
					if (array[i] == array[j]) { return false; }
				}
			}
		}
	}
	else if (type == "string")
	{
		if (!value.isString() || value.toString().size() < schema["minLength"].toInt(0)
			|| value.toString().size() > schema["maxLength"].toInt(65536))
		{
			return false;
		}
	}
	else if (type == "boolean")
	{
		if (!value.isBool()) { return false; }
	}
	else if (type == "integer" || type == "number")
	{
		if (!value.isDouble()) { return false; }
		const double number = value.toDouble();
		if (!std::isfinite(number) || (type == "integer" && std::floor(number) != number)
			|| (schema.contains("minimum") && number < schema["minimum"].toDouble())
			|| (schema.contains("maximum") && number > schema["maximum"].toDouble()))
		{
			return false;
		}
	}
	return true;
}
} // namespace
DomainError validateToolArguments(const QString& action, const QJsonObject& arguments)
{
	static const auto definitions = QJsonDocument::fromJson(McpToolInputSchema).object()["definitions"].toObject();
	QJsonObject schema;
	if (action == "get_clip_summary")
	{
		schema = QJsonObject{{"type", "object"}, {"required", QJsonArray{"clipId"}},
			{"properties", QJsonObject{{"clipId", QJsonObject{{"type", "string"}, {"minLength", 1}}}}}};
	}
	else if (action == "read_artifact")
	{
		schema = QJsonObject{{"type", "object"}, {"required", QJsonArray{"artifactId", "offset", "limit"}},
			{"properties",
				QJsonObject{{"artifactId", QJsonObject{{"type", "string"}, {"minLength", 1}}},
					{"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 16 * 1024 * 1024}}},
					{"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 256 * 1024}}}}}};
	}
	else { schema = definitions["lmms_" + action].toObject(); }
	if (schema.isEmpty()) { return {ErrorCode::UnsupportedOperation, "Unknown domain action."}; }
	if (!valid(arguments, schema)) { return {ErrorCode::ValidationError, "Invalid arguments for " + action}; }
	if (action == "set_transport" && arguments["action"].toString() == "seek" && !arguments.contains("tickPosition"))
	{
		return {ErrorCode::ValidationError, "seek requires tickPosition."};
	}
	return {};
}

DomainError validateCommandSchema(const QJsonObject& command)
{
	static const auto schema = QJsonDocument::fromJson(McpCommandSchema).object();
	for (const auto& option : schema["oneOf"].toArray())
	{
		const auto definition = option.toObject();
		if (definition["properties"].toObject()["kind"].toObject()["const"] != command["kind"]) { continue; }
		if (valid(command, definition)) { return {}; }
		return {ErrorCode::ValidationError,
			"Command does not conform to the published schema: " + command["kind"].toString()};
	}
	return {ErrorCode::UnsupportedOperation, "Unknown command kind."};
}
} // namespace lmms::ai
