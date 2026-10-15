# Add yourself as a player

As an optional thank-you for contributing, you can add your name or nickname
to [CONTRIBUTORS.md](../../CONTRIBUTORS.md), or give a fictional player your
name at your favorite existing club. You can choose both forms of credit.

The game loads hand-made players from `assets/user_made_data/players/*.json`
when it creates a new career. This is the actual folder name in the repository.
The rest of each squad is filled with generated players. A cameo changes the
starting world; it does not insert a player into an existing save or guarantee
that the player will start every match.

## 1. Choose your club and an unused player ID

Find your favorite club in
[the team data](../../assets/user_made_data/teams). Its `id` is your player's
`team_id`. For example, Lecce is team **103** in the default pack. Use an
existing club so that no league or scheduling changes are needed.

Choose a positive player `id` below **50000** that is not already used in
[the player data](../../assets/user_made_data/players). Generated players start
at 50000. The example uses **20001**, unused when this guide was written;
check again before submitting it. If another contribution takes the same ID,
choose another before merging.

For a quick local search:

```sh
rg -n 'Lecce' assets/user_made_data/teams
rg -n '"id"\s*:\s*20001\b' assets/user_made_data/players
```

## 2. Create a player file

Create `assets/user_made_data/players/your-handle.json`. Use lowercase words
and hyphens for the filename. Each file contains a JSON **array**, even if
you add only one player. Replace the example names with your own chosen name
or nickname, change `team_id` to your club, and check the ID is free.

```json
[
  {
    "id": 20001,
    "team_id": 103,
    "first_name": "Alex",
    "last_name": "Contributor",
    "age": 22,
    "nationality": "Italian",
    "role": "CM",
    "height": 180,
    "preferred_foot": "Right",
    "contract_years": 3,
    "wage": 2500,
    "stats": {
      "Pace": 65,
      "Shooting": 55,
      "Passing": 68,
      "Dribbling": 62,
      "Defending": 58,
      "Physicality": 60,
      "Stamina": 70,
      "Vision": 66,
      "Goalkeeping": 10
    }
  }
]
```

`role` accepts `GK`, `CB`, `LB`, `RB`, `CDM`, `CM`, `CAM`, `LM`, `RM`, `LW`,
`RW` and `ST`. Height is in centimetres and wage is weekly. Use `Left` or
`Right` for the preferred foot. Nationality is a demonym, such as `Italian`,
`English` or `French`; supported values are in
[languages.h](../../src/global/languages.h).

Attributes use the names in
[stats_config.json](../../assets/config/stats_config.json) and values from
1 to 100. Keep attributes and wages plausible for the club; a cameo should
fit the squad's level. The example's age, height and nationality describe
the fictional player, so they do not need to be your personal details.

## 3. Check it in a new career

Check the JSON syntax from the repository root:

```sh
python3 -m json.tool assets/user_made_data/players/your-handle.json > /dev/null
```

Then start a development build with an isolated save/settings directory:

```sh
FM_RUNTIME_ROOT=/tmp/fm-player-cameo FM_WORLD_SEED=424242 \
  ./build/release/src/Player12
```

Create a **new career**, choose your club, open its squad and find your player.
Use the executable path from your own build if you used another preset or
followed the [macOS/Windows setup](../user/installation.md). On PowerShell,
set environment variables with `$env:FM_RUNTIME_ROOT` and `$env:FM_WORLD_SEED`
before launching the executable.

## 4. Include it in your contribution

Add the JSON file to your pull request and state the chosen club, player ID
and how you checked the new career. You can also add your name to
[CONTRIBUTORS.md](../../CONTRIBUTORS.md). Use your own name or nickname that
you want publicly credited. The default pack keeps professional players,
real club badges and copied kits out of its content.

If you prefer credits only, editing CONTRIBUTORS.md is enough. For all fields
and the wider world format, see the
[data reference](../../assets/user_made_data/README.md).
