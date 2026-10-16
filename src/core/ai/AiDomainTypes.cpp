#include "AiDomainTypes.h"

namespace lmms::ai
{

QString errorCodeToString(ErrorCode code)
{
	switch (code)
	{
		case ErrorCode::Ok: return "OK";
		case ErrorCode::NotConnected: return "NOT_CONNECTED";
		case ErrorCode::NotReady: return "NOT_READY";
		case ErrorCode::PermissionDenied: return "PERMISSION_DENIED";
		case ErrorCode::WriterBusy: return "WRITER_BUSY";
		case ErrorCode::Busy: return "BUSY";
		case ErrorCode::ProjectChanged: return "PROJECT_CHANGED";
		case ErrorCode::StaleHandle: return "STALE_HANDLE";
		case ErrorCode::StaleCursor: return "STALE_CURSOR";
		case ErrorCode::RevisionConflict: return "REVISION_CONFLICT";
		case ErrorCode::ValidationError: return "VALIDATION_ERROR";
		case ErrorCode::UnsupportedOperation: return "UNSUPPORTED_OPERATION";
		case ErrorCode::PlaybackActive: return "PLAYBACK_ACTIVE";
		case ErrorCode::ParameterControlled: return "PARAMETER_CONTROLLED";
		case ErrorCode::PathNotAllowed: return "PATH_NOT_ALLOWED";
		case ErrorCode::AssetNotFound: return "ASSET_NOT_FOUND";
		case ErrorCode::PluginUnavailable: return "PLUGIN_UNAVAILABLE";
		case ErrorCode::PluginLoadFailed: return "PLUGIN_LOAD_FAILED";
		case ErrorCode::PlanExpired: return "PLAN_EXPIRED";
		case ErrorCode::OperationKeyReused: return "OPERATION_KEY_REUSED";
		case ErrorCode::HistoryConflict: return "HISTORY_CONFLICT";
		case ErrorCode::RenderFailed: return "RENDER_FAILED";
		case ErrorCode::ExportValidationFailed: return "EXPORT_VALIDATION_FAILED";
		case ErrorCode::Timeout: return "TIMEOUT";
		case ErrorCode::Cancelled: return "CANCELLED";
		case ErrorCode::InternalError: return "INTERNAL_ERROR";
	}
	return "UNKNOWN_ERROR";
}

ErrorCode stringToErrorCode(const QString& str)
{
	if (str == "OK") return ErrorCode::Ok;
	if (str == "NOT_CONNECTED") return ErrorCode::NotConnected;
	if (str == "NOT_READY") return ErrorCode::NotReady;
	if (str == "PERMISSION_DENIED") return ErrorCode::PermissionDenied;
	if (str == "WRITER_BUSY") return ErrorCode::WriterBusy;
	if (str == "BUSY") return ErrorCode::Busy;
	if (str == "PROJECT_CHANGED") return ErrorCode::ProjectChanged;
	if (str == "STALE_HANDLE") return ErrorCode::StaleHandle;
	if (str == "STALE_CURSOR") return ErrorCode::StaleCursor;
	if (str == "REVISION_CONFLICT") return ErrorCode::RevisionConflict;
	if (str == "VALIDATION_ERROR") return ErrorCode::ValidationError;
	if (str == "UNSUPPORTED_OPERATION") return ErrorCode::UnsupportedOperation;
	if (str == "PLAYBACK_ACTIVE") return ErrorCode::PlaybackActive;
	if (str == "PARAMETER_CONTROLLED") return ErrorCode::ParameterControlled;
	if (str == "PATH_NOT_ALLOWED") return ErrorCode::PathNotAllowed;
	if (str == "ASSET_NOT_FOUND") return ErrorCode::AssetNotFound;
	if (str == "PLUGIN_UNAVAILABLE") return ErrorCode::PluginUnavailable;
	if (str == "PLUGIN_LOAD_FAILED") return ErrorCode::PluginLoadFailed;
	if (str == "PLAN_EXPIRED") return ErrorCode::PlanExpired;
	if (str == "OPERATION_KEY_REUSED") return ErrorCode::OperationKeyReused;
	if (str == "HISTORY_CONFLICT") return ErrorCode::HistoryConflict;
	if (str == "RENDER_FAILED") return ErrorCode::RenderFailed;
	if (str == "EXPORT_VALIDATION_FAILED") return ErrorCode::ExportValidationFailed;
	if (str == "TIMEOUT") return ErrorCode::Timeout;
	if (str == "CANCELLED") return ErrorCode::Cancelled;
	return ErrorCode::InternalError;
}

QStringList permissionsToStringList(Permission p)
{
	QStringList list;
	if (hasPermission(p, Permission::Read)) list.append("read");
	if (hasPermission(p, Permission::WriteProject)) list.append("write_project");
	if (hasPermission(p, Permission::Assets)) list.append("assets");
	if (hasPermission(p, Permission::Export)) list.append("export");
	if (hasPermission(p, Permission::ManageFiles)) list.append("manage_files");
	return list;
}

Permission permissionsFromStringList(const QStringList& list)
{
	Permission p = Permission::None;
	for (const auto& item : list)
	{
		if (item == "read") p = p | Permission::Read;
		else if (item == "write_project") p = p | Permission::WriteProject;
		else if (item == "assets") p = p | Permission::Assets;
		else if (item == "export") p = p | Permission::Export;
		else if (item == "manage_files") p = p | Permission::ManageFiles;
	}
	return p;
}

} // namespace lmms::ai
