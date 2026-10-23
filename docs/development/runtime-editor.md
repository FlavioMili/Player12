# Runtime editor architecture

The persistence editor is independent of the football schema. The player-facing
UI is scoped to records exposed by the current page, following the product
decision to avoid exposing internal database administration controls.

## Persistence

SQLite 3.37 or newer is required for schema reflection and STRICT tables.

`RuntimeEditor` takes a caller-owned SQLite connection. `catalog` reads
`table_list`, `table_xinfo`, indexes and foreign keys. `read` returns bounded
pages of typed values, including NULL and binary data. Ordinary tables use an
unshadowed rowid; WITHOUT ROWID tables use ordered composite primary keys.
Views, virtual tables and generated/hidden columns are read-only. Value binding
and metadata-verified quoted identifiers keep user text out of SQL syntax.

Every Change includes its schema fingerprint and complete original record.
`apply` checks schema and record freshness, then applies the batch in a
savepoint. Constraints, new foreign-key violations and supplied validators
reject the entire batch, including trigger effects. Private drafts may defer
relationship checks until the complete batch is applied. This API also supports
inserts and deletes for future database creation tools; the gameplay editor
exposes updates only.

## Runtime replacement

GameController owns editor sessions. Beginning a session flushes current
runtime state and makes a private working copy. Apply replays the final drafts
on a fresh copy of the source, runs semantic validators, constructs a private
Game and serializes it. Edited values must survive this roundtrip. Lossy casts,
silently normalized fields and invalid payloads cannot publish a replacement.
The running Game, GameData and connection change only after successful checks.

The controller serializes editing, saving and career advancement with its
non-recursive runtime mutex. Public entry points own the lock; private
`persistLocked` shares that ownership during new-career saves and autosaves,
without acquiring it again. Unemployed advancement follows the same rules.
Sessions pause advancement and saves; the GUI also excludes
live matches and pending Continue operations. GUIView consumes apply/undo
requests before scene updates, then rebuilds all scenes containing model
references and reopens the current navigation entry when it still exists.

Failed applies preserve the original career and the draft. Repeated edits of a
record retain its initial optimistic-lock original and its final field values,
so correcting a rejected value does not replay the rejected value later.
Successful applies retain an in-memory pre-edit database for recovery and set
the save's edited flag. Disk persistence uses the existing atomic SaveManager.

## Preserving custom data

Game compares the active schema with this build's canonical schema to discover
extra columns; this is reflection, not an editor-owned table allowlist. Extra
columns are captured before serialization and restored inside the save
transaction, using primary or natural unique keys. Natural keys preserve fields
on records whose repositories use INSERT OR REPLACE with generated IDs. Deleted
records are not recreated. A schema-cookie change refreshes this discovery.

At load, a private serialization probe identifies opaque members of JSON
objects that current model serializers omit. Only those members are merged
back on subsequent saves; known runtime fields remain free to change or be
removed by gameplay. This covers object members and nested objects, including
opaque array values stored as object members. Extension members inside array
elements require a feature-owned codec with stable element identity; arbitrary
array reordering cannot be inferred safely by the generic editor.

Custom schemas must remain compatible with the running game's loaders and
serializers. The headless editor can work on unrelated SQLite schemas, but an
arbitrary SQLite database does not itself define a playable football world.
Extra required columns on rewritten tables need defaults or writer support.

## Extending a page or feature

A ManagementScene can override `editorContext()` with an EditorContext declaring
its primary table, current-record filter, related tables and their filters.
Alternatively call `GUIView::openEditor(context)`. Declare `editable_fields` for
a custom feature: each `EditorField` binds a column to a localisation key,
section key, control, optional numeric limits and labelled stored choices.
Only declared columns receive controls or enter UI-generated changes. Missing
columns are omitted; keys/generated columns remain read-only. Keep labels and
choice values separate so changing language never changes persisted values.

Built-in player/club presentations live in `gui/view_models/editor_fields.cpp`.
The dialog renders these descriptions without inferring gameplay meaning from
SQL types. New player stat keys still come from the stored stats object and
appear automatically. New ordinary columns remain preserved but require feature
presentation metadata before becoming editable. This is deliberate: a database
schema alone cannot say whether a number is a position, a price or an internal
counter. Core schema discovery, validators and opaque-data preservation remain
independent of the player-facing presentation.

Register new semantic rules through `GameController::registerEditorValidator`.
Validators receive the staged connection and changes, throw a useful diagnostic
on failure, and must not mutate the live game or produce external side effects.
Apply holds the controller's runtime lock while calling validators; callbacks
must not reenter mutating GameController methods.
Keep engine-specific rules near their model boundary; SQL constraints remain
authoritative for database structure.

## Checks

Build `runtime_editor_tests` and `runtime_editor_ui_tests`, then run
`ctest --test-dir build -L editor --output-on-failure`. The headless tests use
unrelated schemas, changed schemas, generated columns, composite keys, NULLs,
binary values, injection-shaped strings, stale records, constraints, triggers,
opaque JSON and custom columns. Career tests cover new stats, other clubs,
save/load and recovery. UI tests exercise contextual opening, runtime refresh,
error correction, custom page contexts, English/Italian and narrow layouts.
