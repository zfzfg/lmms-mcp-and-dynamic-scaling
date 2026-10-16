export enum ErrorCode {
  Ok = "OK",
  NotConnected = "NOT_CONNECTED",
  NotReady = "NOT_READY",
  PermissionDenied = "PERMISSION_DENIED",
  WriterBusy = "WRITER_BUSY",
  Busy = "BUSY",
  ProjectChanged = "PROJECT_CHANGED",
  StaleHandle = "STALE_HANDLE",
  StaleCursor = "STALE_CURSOR",
  RevisionConflict = "REVISION_CONFLICT",
  ValidationError = "VALIDATION_ERROR",
  UnsupportedOperation = "UNSUPPORTED_OPERATION",
  PlaybackActive = "PLAYBACK_ACTIVE",
  ParameterControlled = "PARAMETER_CONTROLLED",
  PathNotAllowed = "PATH_NOT_ALLOWED",
  AssetNotFound = "ASSET_NOT_FOUND",
  PluginUnavailable = "PLUGIN_UNAVAILABLE",
  PluginLoadFailed = "PLUGIN_LOAD_FAILED",
  PlanExpired = "PLAN_EXPIRED",
  OperationKeyReused = "OPERATION_KEY_REUSED",
  HistoryConflict = "HISTORY_CONFLICT",
  RenderFailed = "RENDER_FAILED",
  ExportValidationFailed = "EXPORT_VALIDATION_FAILED",
  Timeout = "TIMEOUT",
  Cancelled = "CANCELLED",
  InternalError = "INTERNAL_ERROR"
}

export interface DomainError {
  code: ErrorCode;
  message: string;
  retryable?: boolean;
  details?: Record<string, unknown>;
}

export class LmmsBridgeError extends Error {
  public readonly code: ErrorCode;
  public readonly retryable: boolean;
  public readonly details?: Record<string, unknown>;

  constructor(domainError: DomainError) {
    super(`[${domainError.code}] ${domainError.message}`);
    this.name = "LmmsBridgeError";
    this.code = domainError.code;
    this.retryable = domainError.retryable ?? false;
    this.details = domainError.details;
  }
}
