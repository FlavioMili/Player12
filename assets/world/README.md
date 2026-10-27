# World databases (`.fmdb`)

A world database is one UTF-8 JSON file holding any part of a football world:
countries, cities, travel times, competitions, clubs, players and name pools.
The game's own world is one too: [`default.fmdb`](default.fmdb).

Most people never need this page. Worlds are made, combined and shared from the
game: **Main menu → World editor**, and every new career asks which world to
play in (see the [World editor guide](../../docs/user/world-editor.md)). This
page is the reference for reading, reviewing or hand-editing the files, for
example in a pull request or a GitHub repository of shared worlds.

## Rules that always hold

- Every section is optional except `manifest`. A file with only players
  ("legends") or only a league is valid; it is combined with other worlds in
  the editor, and missing squads, staff and names are generated.
- Unknown keys are kept: the editor, combining and Export world preserve data
  the game does not use yet, so mods can carry their own fields.
- The game writes files deterministically: fixed top-level order, entities
  sorted by id, one entity per line. Saving an unchanged world twice gives the
  same bytes, and changes diff line by line.
- Ids only link entities inside one file. When worlds are combined, the game
  renumbers the later one; you never need to coordinate ids with other makers.
- Files up to 64 MB are accepted. A 15,000-player world is about 5 MB.
- Nothing is applied until a file is valid. The game lists every problem with
  the section, entity name and field.

## Layout

```json
{
  "manifest": {"name": "Legends", "author": "Me", "version": "1.0", "schema": 2,
               "min_game_version": "1.1.0", "season": 2026,
               "description": "Famous players of the past"},
  "countries": [{"id":1,"name":"Freeland","nationality":"English","region":"Europe","reputation":60,"economy":60}],
  "cities":    [{"id":1,"name":"Northport","country_id":1,"population":50000}],
  "travel":    [{"from":1,"to":2,"minutes":90}],
  "leagues":   [{"id":1,"name":"Freeland League","country_id":1,"tiebreak":"goal_difference"}],
  "clubs":     [{"id":101,"league_id":1,"city_id":1,"name":"Northport","short_name":"NOR"}],
  "players":   [{"id":1,"team_id":101,"first_name":"Sam","last_name":"Example","age":24,"role":"ST","stats":{"Pace":70}}],
  "first_names": {"names": ["Sam"]},
  "last_names":  {"names": ["Example"]}
}
```

`manifest.schema` is 2. Files with `schema: 1` (the first single-file custom
databases) load unchanged: each top division becomes a country. A file with a
newer schema is refused with the game version it needs (`min_game_version`).
`season` is the year the ages refer to; it lets the same player be recognised
in worlds made in different years.

## Fields

Required fields are in **bold**. Ids are positive integers, unique per section.

### countries (id 1-255)

| Field | Meaning |
|-------|---------|
| **`name`** | Display name. |
| **`nationality`** | Demonym of its people, one of the game's 159 nationalities (`"Italian"`, `"Scottish"`, ...; see `src/global/languages.h`). Fantasy countries pick the closest one. Generated players of its clubs mostly have it and draw names from its name pool. |
| `region` | `Europe` (default), `EasternEurope`, `NorthAmerica` or `SouthAmerica`: pre-season tours stay in the region. |
| `reputation` | Football level 1-100 (default 50). |
| `economy` | Economic level 1-100 (default: `reputation`). Drives revenue and crowds of leagues without an authored profile. |

### cities (id 1-65535) and travel

Cities have **`name`**, **`country_id`** and `population`. Travel rows link
two cities, **`from`** and **`to`**, with **`minutes`** (1-10000); a pair
appears once in either direction. Populations and travel times are stored and
editable but do not affect matches or finances yet.

### leagues (id 1-249)

| Field | Meaning |
|-------|---------|
| **`name`** | Display name. A top division's domestic cup is named after it ("League" becomes "Cup"). |
| `country_id` | Country. Without one the league gets a placeholder profile (warning). |
| `parent_league` | Division above; links tiers for promotion and relegation (three up, three down). A league without a parent is a top division. |
| `tiebreak` | `goal_difference` (default) or `head_to_head`. |
| `profile` | Economy and match profile (below). |
| `round_days` | Optional match-day pattern (see the built-in leagues). |

`profile` fields are all optional; any value left out is derived from the
country's `reputation`/`economy` and the division (each tier about 25
reputation points lower):
`reputation` (1-100), `average_revenue_eur`, `tv_share`,
`continental_share`, `gate_share`, `commercial_share` (shares 0-1),
`wage_ratio` (wages / revenue), `average_attendance`, `stadium_fill` (0-1),
`domestic_nationality`, `domestic_share` (0-1), `region`, `coach_changes`
(per club and season), `shape` {`level_sd`, `elite_clubs`, `elite_gap`,
`level_offset`} (how unequal the clubs are) and `match_style` {`goals`,
`yellow_cards`, `referee_spread`, `home_edge`} (real match averages the
engine is steered towards).

The built-in profiles are researched values: revenue, mix and wage ratios of
the top five follow UEFA ECFIL 2025 (FY2024), with France after its 2024-25
TV collapse; Brazil, Russia and the USA use national reports. Second tiers
use the Championship (~EUR 46m, wages above 90% of revenue), 2. Bundesliga
(~EUR 50m, wages ~40%) and Serie B (EUR 24m at 82%); the rest are estimates.
Crowds and stadium fill follow 2024-25 league averages; domestic shares follow
CIES expatriate shares (second tiers about half the expatriates). Shapes are
fitted to 20-club points curves, coach changes follow LMA/CIES counts and
match styles are football-data 2022-26 averages.

### clubs (id 1-65535)

| Field | Meaning |
|-------|---------|
| **`name`** | Display name, unique within its league. |
| **`league_id`** | League the club starts in. |
| `city_id` | Home city. |
| `short_name` | Three-letter code, unique in the world. |
| `nickname`, `founded` | Club nickname, founding year. |
| `colours.primary`, `colours.secondary` | Kit colours `"#RRGGBB"`. |
| `colours.home`, `.away`, `.third` | Full strips: `shirt`, `shorts`, `socks`, `trim` as `"#RRGGBB"`. The badge and UI accents follow the home strip. |
| `stadium.name`, `stadium.capacity` | Stadium. The capacity used by the simulation follows the club's reputation. |
| `balance` | Ignored for new careers (opening cash follows reputation). |
| `article_it` | Italian article (`"il"`, `"lo"`, `"l'"`, `"la"`) when the default is wrong. |

### players (id 1 and up)

| Field | Meaning |
|-------|---------|
| **`first_name`**, **`last_name`** | Names. `nickname` is shown as First "Nick" Last. |
| **`age`** | 15-60. |
| **`role`** | Position: `GK`, `CB`, `LB`, `RB`, `CDM`, `CM`, `CAM`, `LM`, `RM`, `LW`, `RW` or `ST` (the older `Goalkeeper`, `Defender`, `Midfielder` and `Striker` are accepted). |
| **`stats`** | Attribute name → 0-100. The nine standard attributes are `Pace`, `Shooting`, `Passing`, `Dribbling`, `Defending`, `Physicality`, `Stamina`, `Vision`, `Goalkeeping`; missing ones take the mean of those given. |
| `team_id` | Club; absent or 0 = free agent. |
| `nationality` | Demonym; unknown values fall back to English (warning). |
| `preferred_foot` | `Left`, `Right` (default) or `Both`. |
| `height`, `skin` | Centimetres (100-250); skin tone 0 (lightest) - 7. |
| `wage`, `contract_years` | Weekly wage, contract length. |

Every club is filled up to a full squad with generated players.

### Name pools

`first_names` and `last_names` each hold `names` (the general list),
optionally `by_nationality` (demonym → list) and, for first names,
`excluded_name_hashes`: full names that must never be generated, stored as
16-digit hex `NamePool::nameHash` values (salted FNV-1a of the lower-case,
accent-folded name) so the file carries no real names. Worlds without pools
use the built-in ones; combining worlds merges pools.

## Combining worlds

The World editor can load several worlds into one. The same thing defined in
two worlds is recognised by name: countries by name, leagues/clubs/cities by
name within their country, players by name, nationality and birth year.
Identical copies merge silently; copies with different values become
conflicts you resolve (keep one, choose field by field, keep both, or use one
world as the source of truth for all the rest). Similar names ("Real Madrid"
and "Real Madrid CF") are only flagged.

## Before a career can start

A world can be saved at any stage, but a career needs at least one league
with four or more clubs, an even number of clubs in every league, no loops of
parent leagues, unique three-letter codes, unique club names per league and
at most 60 players per club. The editor lists what is missing.

## Content rules for the repository

The built-in world keeps real city names for clubs but contains no real
crests, kits, stadiums or professional players. Contributors may add their own
name as a fictional player (see the [player cameo guide](../../docs/contributing/player-cameo.md)).
Share worlds based on real people and clubs from your own repositories, not
from this one.
