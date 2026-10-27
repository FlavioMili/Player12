# Default world data pack

For a complete asset-copy workflow, current modding limits and C++ behavior
extensions, see [Extending and modding](../../docs/development/extending-and-modding.md).

This directory is the world that a new career starts from: eleven
countries, each with a top division and a second division linked for
promotion and relegation (three clubs up, three down every season). Clubs
are named after real cities and towns (including the Apulian towns of the
Italian second division, Acaya among them), but the pack contains **no real crests, kits,
stadiums or professional players**: kit colours, nicknames, founding years
and player identities are fictional, with optional contributor-name cameos.
Stadiums use a generic municipal naming
pattern (which can coincide with a real municipal stadium's name). Keep
professional players, real club badges and copied kits out of the pack.
Contributors may use their own name or nickname for a fictional player as an
optional credit; follow the [player cameo guide](../../docs/contributing/player-cameo.md).

Files are read at startup from the asset root (`AssetPaths` in
`src/global/paths.h.in`: installed data next to the executable, else the
source checkout).
Every `.json` file in `teams/` and `players/` is loaded, so a data pack can
be split over as many files as you like. All files are UTF-8 JSON.

## `leagues/leagues.json`

Array of leagues.

| Field           | Required | Meaning                                                         |
|-----------------|----------|-----------------------------------------------------------------|
| `id`            | yes      | League id (1-249; 250 and up are continental competitions). Also keys the economy/nationality/region profile in `src/model/world_tuning.h` (`LEAGUE_PROFILES`): add a row for every new league, otherwise it silently gets a default English, European profile. |
| `name`          | yes      | Display name. The domestic cup of a top division is named after it ("League" becomes "Cup"). |
| `parent_league` | no       | Id of the division above; links the tiers of one country for promotion and relegation. A league without a parent is a country's top division: it names the domestic cup (entered by every division of the country) and is the country for continental places and scouting. |
| `tiebreak`      | no       | `"head_to_head"` or omitted for goal difference.                |

## `teams/*.json`

Array of clubs. The default pack uses 20 clubs per league; keep leagues at
an even size and check `src/model/match_scheduler.*` before changing it.
Team ids follow the league: league `L` holds ids `L*100+1` to `L*100+20`
(for example 1401-1420 for the English second division). The second
divisions of every country except Italy are in `second_divisions.json`.

| Field                 | Required | Meaning                                                   |
|-----------------------|----------|-----------------------------------------------------------|
| `id`                  | yes      | Unique team id (1-65535; 0 is reserved for free agents). Keep ids stable: saves refer to them. |
| `name`                | yes      | Display name.                                             |
| `league_id`           | yes      | League the club starts in.                                |
| `balance`             | no       | Ignored for new worlds (opening cash follows reputation). |
| `short_name`          | no       | Three-letter code, unique in the pack, e.g. `"LEC"`.      |
| `colours.primary`     | no       | Kit colour `"#RRGGBB"`.                                   |
| `colours.secondary`   | no       | Kit colour `"#RRGGBB"`, different from the primary.       |
| `stadium.name`        | no       | Stadium name.                                             |
| `stadium.capacity`    | no       | Nominal capacity (see below).                             |
| `founded`             | no       | Founding year.                                            |
| `nickname`            | no       | Club nickname.                                            |
| `article_it`          | no       | Italian article (`"il"`, `"lo"`, `"l'"`, `"la"`) when the default (masculine, chosen from the first letters) is wrong, e.g. `"la"` for Roma. |

The optional fields are read by `DataGenerator::loadClubIdentities()` (and
`article_it` by `loadClubArticles()`) and are
not stored in saves. Reputation, facilities, ticket prices and the stadium
capacity used by the simulation are generated from the world seed, so
`stadium.capacity` is descriptive only for now.

## `players/*.json`

Optional hand-made players. Every club is filled up to a full squad with
generated players, so this directory may be empty.

| Field            | Meaning                                                       |
|------------------|---------------------------------------------------------------|
| `id`             | Unique player id below 50000 (generated players start there). |
| `team_id`        | Club id.                                                      |
| `first_name`, `last_name` | Fictional names, or your own chosen contributor name/nickname. |
| `age`, `height`  | Years, centimetres.                                           |
| `nationality`    | Demonym such as `"Italian"` (see `stringToLanguage` in `src/global/languages.h`; unknown values fall back to English). |
| `role`           | Role name understood by `RoleUtils::fromString`.              |
| `stats`          | Map of attribute name to 1-100 value. The nine attributes are `Pace`, `Shooting`, `Passing`, `Dribbling`, `Defending`, `Physicality`, `Stamina`, `Vision` and `Goalkeeping` (see `possible_stats` in `assets/config/stats_config.json`). |
| `contract_years`, `wage` | Contract length and weekly wage.                      |
| `preferred_foot` | `"Left"` or `"Right"`.                                        |
| `status`         | Optional, defaults to 0.                                      |

## `names_files/`

`first_names.json` and `last_names.json` are the name pools for generated
players, staff and scouts.

| Field            | Required | Meaning                                                   |
|------------------|----------|-----------------------------------------------------------|
| `names`          | yes      | Array of names, used for nationalities without a pool of their own. Must not be empty. |
| `by_nationality` | no       | Object mapping a nationality demonym (as for players, e.g. `"Italian"`) to an array of names. Generated players and staff of that nationality draw from it; unknown demonyms and empty arrays are ignored. |

`first_names.json` may also carry a hashed exclusion list,
`excluded_name_hashes`: full names the generator must never produce, stored
only as hashes so the pack contains no real people's names. Each entry is the
16-digit lowercase hex form of `NamePool::nameHash` (salted 64-bit FNV-1a of
the name in lower case, Latin accents folded, single spaces; see
`src/model/world_generation.cpp`). A generated first and last name pair whose
hash is on the list is drawn again. Do not add plain names to the pack for
this purpose.

## Custom databases (`*.fmdb`)

A custom database is one JSON file (extension `.fmdb`) that replaces this
pack for new careers. Import it in-game (Main menu → Custom database, or
Settings → Data); the active database is stored under the writable runtime
root and recorded in `save_meta.packs`, so the repository itself never
contains real-world data. Player `nickname`, `preferred_foot: "Both"`,
`skin` (0-7) and full `home`/`away`/`third` kit strips from the pack format
are all honoured.

```json
{
  "manifest": {"name": "My Database", "author": "Me", "version": "1.0",
               "schema": 1, "min_game_version": "1.0.0"},
  "leagues": [ {"id": 1, "name": "Serie A", "tiebreak": "head_to_head"} ],
  "clubs": [ {"id": 101, "league_id": 1, "name": "Lecce",
              "short_name": "LEC", "nickname": "I Giallorossi",
              "colours": {"primary": "#EAB543", "secondary": "#C0392B",
                          "home": {"shirt": "#EAB543", "shorts": "#C0392B",
                                   "socks": "#EAB543", "trim": "#FFFFFF"}},
              "stadium": {"name": "Stadio Comunale", "capacity": 31000},
              "founded": 1908} ],
  "players": [ {"id": 1001, "team_id": 101, "first_name": "Kilian",
               "last_name": "Dubois", "nickname": "Kiki", "age": 24,
               "nationality": "French", "role": "ST",
               "stats": {"Pace": 75, "Shooting": 80},
               "contract_years": 3, "wage": 55000, "height": 182,
               "preferred_foot": "Both", "skin": 2} ],
  "first_names": {"names": ["Luca", "Mateo"]},
  "last_names": {"names": ["Rossi", "Dubois"]}
}
```

Player `stats` holds the nine pack attributes (`Pace`, `Shooting`,
`Passing`, `Dribbling`, `Defending`, `Physicality`, `Stamina`, `Vision`,
`Goalkeeping`), each 0–100.

`manifest.schema` must be `1`. `leagues` and `clubs` are required and
non-empty; `players` may be empty (short squads are generated to 30 as
usual); `first_names`/`last_names` are optional and fall back to the pack.
Every `league_id` must reference a league in the file and every `team_id` a
club in the file; ids and names are unique within their section. Unknown
player roles, malformed `#RRGGBB` colours and dangling references are import
errors; unknown nationalities fall back to English with a warning. The
importer reports every finding per field before anything is applied.
