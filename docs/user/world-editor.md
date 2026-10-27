# Worlds: play, build and share

A **world** is everything a career starts from: countries, competitions,
clubs, players, kits and league economies. The game ships with a built-in
world, and you can play in worlds other players made, build your own, or
combine several into one.

## Start a career in another world

Every new career asks which world to play in, right after you choose the save
slot:

- **Default world**: the built-in world, generated fresh from a random seed.
- **Edit a world first**: opens the World editor; press **Start career** there
  when you are done.
- **Your worlds**: worlds you imported or saved. Select one to see its author,
  version and size, then press **Start career**.

To use a world someone shared, download its `.fmdb` file, then press
**Import file…** or simply drop the file onto the game window. The file is
checked before anything happens: if something is wrong you see each problem
with the club, player or competition it concerns, and nothing is imported.
The game never downloads files itself.

A world only affects the career you are starting. Your other careers keep
the world they were created in, even if you later edit or delete that world.
Clubs and players a world leaves out are generated as in the default world,
so a world of a few clubs or a list of legendary players is enough.

## Build a world

Open **Main menu → World editor**. It opens the built-in world; use **New**
to start from a small example world or an empty one, or **Open** for one of
your worlds or any `.fmdb` file.

The left side lists the layers of the world:

| Page | What you edit |
|------|---------------|
| World details | Name, author, version, description and the season ages refer to |
| Countries | Name, nationality of its people, region, football and economic level |
| Cities and Travel | Cities with their population and travel times between them (stored for future features; no effect on matches yet) |
| Competitions | Divisions, the division above for promotion and relegation, tie-break rule and the economy and match profile |
| Clubs | Name, three-letter code, competition, city, colours and full kits, stadium |
| Players | Names, nickname, club, age, nationality, position, foot, attributes and contract |

Pick a page, search or filter the list (for example the players of one club,
or free agents), and select an entry to edit it with the same controls as the
[in-game editor](in-game-editor.md). **Add**, **Duplicate** and **Delete**
work on the selected page; to move a club to another division or a player to
another club, change its competition or club in the form. Deleting asks first
and tells you what changes: the players of a deleted club become free agents,
the clubs of a deleted division wait without a competition until you place
them. **Undo** and **Redo** step through your changes.

Leagues you create follow their country's football and economic level:
lower divisions are weaker and poorer. Set the profile fields of a
competition only if you want to tune its finances or match style yourself.

## Problems

The counters at the top show errors, problems that block a career and
warnings; select one to see the list and jump to the entry concerned. You can
save an unfinished world at any time. A career needs at least one division of
four or more clubs, an even number of clubs per division, unique club codes
and every club placed in a competition.

## Combine worlds

**Combine** loads another world into the one you are editing, like building
blocks: for example a file of historic players, someone's league and your own
fantasy league.

- Ids never clash: the added world is renumbered automatically.
- Identical entries (the same club with the same data) merge silently.
- The same club, player, competition or country with different data becomes a
  **conflict**. For each one, keep your version, keep the other one, keep
  both, or pick field by field. With many conflicts, choose one world as the
  source of truth for all those remaining.
- Similar names, such as "Real Madrid" and "Real Madrid CF", are only
  flagged: merge them or keep them separate.

Players of a club that merged into another follow the club that remains.

## Save and share

**Save** keeps the world in your worlds, ready for a new career. **Export…**
writes a `.fmdb` file anywhere, for example to share it on GitHub or Discord.
From a running career, **Settings → World databases → Export world…** writes
the world as it is now, with every club and player.

World files are plain text (JSON) written in a stable order, one entry per
line, so they work well in a Git repository and can be reviewed or tweaked in
a text editor. The [format reference](../../assets/world/README.md) describes
every field. Do not publish worlds containing real people's or clubs' data
where you are not allowed to.
