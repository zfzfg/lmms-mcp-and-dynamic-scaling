# Export examples

Use actual project, operation, instance and job IDs from the connected session. Saving a project copy is independent of rendering; a render uses the live snapshot without requiring a preceding copy.

## Save a new project copy

Assume fresh project data gives `proj_demo` at revision 300 and effective permissions include `export`:

```json
{
  "tool":"lmms_save_project_copy",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":300,
    "operationKey":"copy-demo-001",
    "fileName":"session-review-001.mmp"
  }
}
```

Check `ok` and report the returned `savedPath`. The input is a safe basename in the export directory. Existing files are not overwritten, and no `overwrite` or directory argument is available.

If a name exists and the task permits choosing a new name, choose a distinct basename and retain a new operation key for that new intent. If the user specifically requires overwriting, explain that this tool cannot satisfy that part of the request.

## Start a WAV mixdown

Read project state again. In this example it is still revision 300:

```json
{
  "tool":"lmms_start_render",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":300,
    "operationKey":"render-demo-001",
    "outputFileName":"session-review-001.wav",
    "format":"wav",
    "sampleRate":44100,
    "bitDepth":16
  }
}
```

These settings match the API defaults; use explicit user-requested supported settings instead. Successful start returns a `jobId`, not a completed WAV.

## Recover a lost start response

If the start response times out, retain that entire request unchanged:

```json
{"tool":"lmms_get_operation","arguments":{"operationKey":"render-demo-001"}}
```

If outer `ok` is true and `data` contains the original successful response with a job ID, use it. If `data.ok` is false, handle that terminal failure. If unresolved in the same instance, follow the shared exact-retry procedure; do not issue a second start with a new key.

## Follow the existing job

Assume start or lookup returned `job_render_demo`:

```json
{"tool":"lmms_get_job","arguments":{"jobId":"job_render_demo"}}
```

Poll at a moderate interval until a terminal state:

| Job state | Meaning and action |
| --- | --- |
| `queued`, `preparing_snapshot`, `running`, `validating` | Still in progress; retain this job and poll again |
| `succeeded` | Inspect verified output metadata and deliver the available result |
| `failed` | Report the job's error, such as missing/changed resources, worker failure, invalid WAV or output publication failure |
| `cancelled` | Report cancellation; there is no successful artifact to deliver |

On `succeeded`, use the job's actual snapshot revision, artifact ID/path, checksum, byte size, duration, sample rate and bit depth. Neither progress near 100 nor a filename in the request proves success.

## Retrieve the result

If `instanceId` is `inst_demo` and the successful job returns `artifactId: art_mix_demo`, read the discovered audio resource:

`lmms://instances/inst_demo/artifacts/art_mix_demo`

The host's resource-read facility returns WAV content when it is within the 16 MiB resource limit and checksum verification succeeds. Do not call the native internal `read_artifact` action as an invented MCP tool.

For example, a successful 20 MiB WAV exceeds the resource limit but can still be a valid export. Report its returned local `artifactPath` and metadata. Do not describe resource size refusal as a failed render or claim the file was attached.

An integrity error during retrieval is different: do not deliver the changed/corrupt bytes as verified audio. Inspect the existing job and report the retrieval failure.

## Cancel only the intended job

If the user requests cancellation of this active job:

```json
{"tool":"lmms_cancel_job","arguments":{"jobId":"job_render_demo","operationKey":"cancel-render-demo-001"}}
```

This call has no project/revision fields. Poll the job again afterward. A job that finished before cancellation may already be `succeeded`; report the observed terminal state.

Failed starts, snapshot-resource changes, worker crashes, timeouts, cancellations and invalid WAVs are distinct outcomes. Correct the demonstrated cause before creating a new render intent; never promise success from source/schema validation alone.
