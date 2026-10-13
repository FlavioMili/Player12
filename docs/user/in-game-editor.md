# In-game editor

Use the pencil in the career header to edit the player, club or records on
the current page. A player profile opens directly on that player's attributes;
a squad opens on that club's players. **Related items** offers the gameplay
records connected to the page, rather than a browser of internal save tables.

Player ratings are grouped like the player profile. Drag a slider, or
Ctrl-click it to enter an exact value. Attributes supplied by custom data also
appear; unfamiliar attribute names are grouped under Other. **Details** holds
the player's ordinary identity and contract values.

Press **Apply** to check and activate the changes, or **Cancel** to discard the
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
