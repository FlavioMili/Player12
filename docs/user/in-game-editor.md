# In-game editor

Use the pencil while viewing a player or club. A player profile opens on that
player's attributes; a club page opens on the club's details. The **Player /
club** menu lets you switch between the club and its players. A squad page
first asks which player you want to edit.

Use the horizontal section menu to switch between **Attributes**, **Personal
details** and **Contract**. Ratings are grouped like the player profile: adjust a
coloured value using −/+ or click it to type an exact rating. Custom attributes appear
automatically under Other. Personal details use named position, nationality and
preferred-foot choices. Money, age and height controls show their units.

Clubs have **Club details** and **Finances** sections for their name, reputation,
stadium, facilities, balance and budgets. Internal save records, identifiers,
status codes and serialized settings are not editing options. Other pages
explain when they have no gameplay controls; use a player or club page instead.

Press **Apply** once to check and activate the changes, or **Cancel** to discard the
draft. Changes appear immediately and are written to disk by the normal save
action. The career clock pauses while the editor is open. Editing is unavailable
during Continue, holiday and a live match.

Invalid values leave the editor open with an explanation. Correct them and
apply again; failed changes do not replace the running career. **Undo last
edit** restores the complete pre-edit world, including any gameplay progress
since the edit. The recovery snapshot lasts for the current loaded session;
normal save backups remain available through the save manager.

The player-facing editor edits gameplay values. Internal IDs, schema/version
metadata, binary values, raw serialized settings and record creation/deletion
are handled by the game's own features rather than exposed here.
