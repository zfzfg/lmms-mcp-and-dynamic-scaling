# Maintenance and validation

Read this when updating or reviewing the suite, not before ordinary LMMS tasks.

## Ownership and source baseline

The repository's `skills/` directory is the versioned source. Install the eight sibling `lmms-*` skill directories together into the user's skill directory. Keep their relative layout intact; do not flatten references or copy only the entrypoint.

The initial suite was checked against LMMS MCP contract version 1.0 and repository commit `67c30eb20` on 2026-10-17. This records provenance, not a required runtime version. Discover the installed interface before operating it.

Maintainer pointers inside an LMMS checkout:

| Source | What to verify |
| --- | --- |
| `tools/mcp/src/tools/index.ts` and `contracts.ts` | Advertised tools, dynamically appended mutation fields, command schemas and result fields |
| `tools/mcp/src/resources.ts` and `prompts.ts` | Resource templates and optional prompt messages; prompts do not expand authorization |
| `src/core/ai/AiApplicationFacade.cpp` | Containers, actual return values, dedicated actions, catalogs and parameter/automation reads |
| `src/core/ai/AiEditTransaction.cpp` and `AiChangePlan.cpp` | Native command constraints, replacement behavior, local references, atomicity and no-op results |
| `src/core/ai/AiCommandDispatcher.cpp` and `AiLocalServer.cpp` | Effective permissions, operation deduplication, retention, connection invalidation and errors |
| `src/core/ai/AiParameterRegistry.cpp`, `AiJobManager.cpp` and `AiFilePolicy.cpp` | Exposed parameter units, render states/metadata and file/asset restrictions |
| `include/TimePos.h`, `src/core/Track.cpp` and `src/core/PatternStore.cpp` | Tick/step conversion and PatternStore slot positions |
| `doc/mcp/schemas/` | Exported tool-input/output and command schemas |
| `doc/mcp/implementation-status.md` | Actual acceptance evidence and unresolved application/audio/platform gates |

These are source pointers for maintainers, not files an installed skill needs to open. Do not copy entire protocol manuals or schemas into every skill. Update shared guidance once, then update affected examples.

## Structural and schema checks

- Run the `skill-creator` validator on each of the eight skill folders.
- Parse all frontmatter and `agents/openai.yaml` files. Keep names consistent with folders, UI text in English and automatic discovery enabled. No hard-coded host namespace or user-specific installation path belongs in the instructions.
- Resolve every relative Markdown link in both the repository suite and its installed copy. The only operating dependencies are sibling skills and the connected LMMS tools.
- Parse all fenced JSON examples. For examples with `tool` and `arguments`, validate the argument object against that tool's definition in `mcp-tools-input.schema.json`. Validate each preview command against `mcp-commands.schema.json` too.
- Check native semantics in addition to schemas: valid owner/container, correct time origin, local reference order, parameter units, automation replacement, note budgets and known history groups.
- Compare repository and installed file lists and SHA-256 hashes after installation.

At the initial baseline, examples cover all 22 advertised tools and all 20 change-command kinds. These counts are coverage observations, not constants that should reject a future expanded server.

If companion code or contracts change, use the existing companion tests and schema-drift check as appropriate. A documentation-only suite update does not require rebuilding all of LMMS.

## Acceptance exercises

Use representative tool responses to walk through these decisions without modifying a user's live project:

| Scenario | Expected behavior |
| --- | --- |
| Tools discover, status returns `NOT_CONNECTED` | Check host configuration and the selected connection file; do not infer readiness from discovery |
| Multiple instance files are plausible | Use an explicitly selected instance or ask for the missing choice |
| Native profile grants read only | Inspect normally; do not apply changes or assume asset/export permission |
| Another host owns the writer slot | Observe read-only or arrange release of the intended writer; do not kill unrelated hosts |
| Status is connected but the project is loading | Wait for readiness and re-read project identity before work |
| A multi-page read encounters a changed revision | Discard mixed pages and restart the query; keep opaque cursors opaque |
| GUI editing invalidates a prepared plan | Re-read affected content and preview the intended delta at the new revision |
| A previously read note handle becomes stale | Re-read notes and select the intended musical events again |
| A plan expires before apply | Resolve any uncertain earlier apply, then create a fresh preview |
| Apply response is lost; lookup contains the original success | Consume the original result and verify state; do not duplicate the change |
| Lookup outer `ok` is true but nested `data.ok` is false | Handle the original failure; do not report a commit |
| Operation lookup is absent but the same instance remains | If retrying, send the original key and exact original arguments |
| LMMS restarted after an uncertain operation | Re-read and reconcile current content; do not assert persistent operation recovery |
| User edited after the agent's known undo group | Honor `HISTORY_CONFLICT`; do not undo more groups or guess the new head |
| Four-bar phrase at bar 5, 48 TPQ and 4/4 | Start the clip at 768 song ticks; use local note positions within its 768-tick span |
| The same request in 3/4 | Use 144 ticks per bar, a 576-tick span and newly calculated phrasing |
| New beat request without a PatternStore setup | Use a song-track solution only if it meets the intent; do not invent PatternTrack creation |
| Several PatternStore clips exist on one instrument | Match the song patternIndex to the observed store slot; do not edit the first clip by default |
| Existing one-bar step pattern needs complete replacement | Explicitly set both desired enabled and disabled steps within the requested span |
| One automation midpoint changes | Read all points and send the full intended replacement with preserved endpoints |
| Existing automation is cubic, multi-target or has split values | Explain that the exposed write cannot preserve that structure before a lossy change |
| Mixer channel is created and then assigned | Resolve its returned handle and new revision before a second assignment/routing plan |
| Export filename already exists | Use a new permitted name when allowed, or explain unsupported overwrite |
| Render job is still validating | Keep polling the existing job; do not deliver an unverified export |
| Render fails or is cancelled | Report that terminal state and error; do not claim an artifact |
| Successful WAV exceeds 16 MiB | Report its verified local path and metadata; resource refusal is not render failure |
| A resource checksum fails | Report integrity failure; do not claim verified delivery |
| User requests stems, an unavailable plugin or an arbitrary synth knob | Explain the current capability boundary without inventing calls |

These are scenario walkthroughs and schema checks, not evidence that a model followed the skill in a live editor. Full native application, audible synthesis, undo/redo and installed-platform acceptance require a suitable LMMS build and separate integration testing.
