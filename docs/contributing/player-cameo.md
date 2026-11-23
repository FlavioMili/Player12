# Add yourself as a player

As an optional thank-you for contributing, you can add your name or nickname
to [CONTRIBUTORS.md](../../CONTRIBUTORS.md), or give a fictional player your
name at your favorite existing club. You can choose both forms of credit.

The game's built-in world is one file,
[`assets/world/default.fmdb`](../../assets/world/default.fmdb). Its `players`
section holds the hand-made players; the rest of each squad is generated when
a new career starts. A cameo changes the starting world; it does not insert a
player into an existing save or guarantee that the player will start every
match.

## 1. Add the player in the World editor

1. Run a development build from your checkout (see [builds](../development/builds.md)).
2. **Main menu → World editor**. The built-in world opens.
3. Open **Clubs**, select your favorite club, then **Players → Add**. The new
   player already belongs to the selected club.
4. Fill in the name you want to be credited with, position, age, nationality
   and attributes. Keep attributes and wage plausible for the club's level;
   age, height and nationality describe the fictional player, not you.
5. **Export…** and save over `assets/world/default.fmdb` in your checkout.

The editor writes the file deterministically, one entity per line, so your
pull request shows a single added line in the `players` section.

## 2. Or add one line by hand

Add one line to the `players` array of `assets/world/default.fmdb`, keeping
the array sorted by `id`:

```json
{"age":22,"contract_years":3,"first_name":"Alex","height":180,"id":20001,"last_name":"Contributor","nationality":"Italian","preferred_foot":"Right","role":"CM","stats":{"Defending":58,"Dribbling":62,"Goalkeeping":10,"Pace":65,"Passing":68,"Physicality":60,"Shooting":55,"Stamina":70,"Vision":66},"team_id":103,"wage":2500},
```

- `team_id` is your club's `id` in the `clubs` section (Lecce is **103**).
- `id` must be unused in the `players` section; use a number below 50000.
- `role` is one of `GK`, `CB`, `LB`, `RB`, `CDM`, `CM`, `CAM`, `LM`, `RM`,
  `LW`, `RW`, `ST`; `preferred_foot` is `Left`, `Right` or `Both`;
  `nationality` is a demonym such as `Italian` (see
  [languages.h](../../src/global/languages.h)).
- Attributes are the nine names in
  [stats_config.json](../../assets/config/stats_config.json), 1 to 100.

Opening and saving the file in the World editor once normalises the
formatting and reports any mistake with the player's name and field.

## 3. Check it in a new career

Start a development build with an isolated save/settings directory:

```sh
FM_RUNTIME_ROOT=/tmp/fm-player-cameo FM_WORLD_SEED=424242 \
  ./build/release/src/Player12
```

Create a **new career** with the default world, choose your club, open its
squad and find your player. Use the executable path from your own build if
you used another preset or followed the
[macOS/Windows setup](../user/installation.md). On PowerShell, set
environment variables with `$env:FM_RUNTIME_ROOT` and `$env:FM_WORLD_SEED`
before launching the executable.

## 4. Include it in your contribution

Include the changed `assets/world/default.fmdb` in your pull request and state
the chosen club, player ID and how you checked the new career. You can also
add your name to [CONTRIBUTORS.md](../../CONTRIBUTORS.md). Use your own name or
nickname that you want publicly credited. The built-in world keeps
professional players, real club badges and copied kits out of its content.

If you prefer credits only, editing CONTRIBUTORS.md is enough. For all fields
and the wider world format, see the
[world database reference](../../assets/world/README.md).
