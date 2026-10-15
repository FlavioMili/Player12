# Saves, settings and troubleshooting

Saves, settings, logs and screenshots go to the user data directory, never
next to the executable:

| System | Folder |
|--------|--------|
| Linux | `$XDG_DATA_HOME/FlavioMili/FootballManagement` (usually `~/.local/share/FlavioMili/FootballManagement`) |
| macOS | `~/Library/Application Support/FlavioMili/FootballManagement` |
| Windows | `%APPDATA%\FlavioMili\FootballManagement` |

Saves are SQLite databases in `saves/` (one per slot, with numbered
backups beside it that the main menu can restore). The log and any crash
reports are in `logs/`; attach both when you report a crash.

## Recover a save

If a slot cannot load, use the main menu's backup restoration option. Keep a
copy of the affected save and its backups when reporting the problem. The
[first-career guide](getting-started.md) covers normal saving and loading.

## Report a problem

Open a [bug report](https://github.com/FlavioMili/FootballManagement/issues/new/choose)
with your operating system, game version or source revision, steps to reproduce,
and the expected versus actual behavior. Attach a screenshot for visual issues
and logs/crash reports for crashes. Share a save only if you are comfortable
making the names and career details it contains public.

For build problems, include the failing command and its error output; the
[installation guide](installation.md) lists platform prerequisites. For match
behavior, include the fixture and seed if known.

## Use an isolated test career

From a source checkout on Linux, this keeps experiments separate from your
normal saves and settings:

```sh
FM_RUNTIME_ROOT=/tmp/fm-test-career FM_WORLD_SEED=424242 \
  ./build/release/src/Player12
```

Use the executable path for your own build. For data modifications, follow
[Extending and modding](../development/extending-and-modding.md).

## Player12 and earlier careers

Player12 was previously called Football Management. The executable and package
names now use Player12. The save/settings directory intentionally keeps the
historical `FlavioMili/FootballManagement` name, so existing careers and backups
remain available without moving files.
