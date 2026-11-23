# Controls

Player12 is played with the mouse. Keyboard shortcuts speed up the
things you do all the time: moving between screens, advancing the calendar and
driving the match view.

The keys below are the defaults. Every shortcut except F12 (screenshot) and
Shift (sprint in play mode) can be rebound in *Settings > Controls*, which
warns you when two actions would share a key and can reset any of them to
its default. The in-game *Help* screen (F8) lists the keys as you have
bound them.

## Anywhere in the game

| Key | Action |
|-----|--------|
| F12 | Save a screenshot as `captures/screenshot.bmp` in the user data directory (see [Where your files are](#where-your-files-are)). Set `FM_SCREENSHOT_PATH` to choose another file. |

## Main menu and club choice

| Key | Action |
|-----|--------|
| Esc | Close the open dialog (slot list, backups, a confirmation). With a confirmation open above the slot list, only the confirmation closes. |
| Esc (club choice) | Back to the main menu, like the Back to menu button. The new career stays in its slot as "not started"; load it to choose a club later. |

Money fields accept either decimal mark and the usual units: `1,5M`,
`1.5M`, `1500k`, `1.500.000`, `1,500,000`, `€ 2,25 mln` or `850 mila`. The
amount you typed is shown under the field as you type. Text that could mean
two amounts (`1.500,000`, or `1,500k` in English) is refused with a short
explanation instead of being guessed.

## Management screens

These work on every screen of a career (Home, Squad, Transfers and so on).
Shortcuts that would clash with typing are ignored while a text field is
active or a dialog is open.

| Key | Action |
|-----|--------|
| Ctrl+K | Open the command palette: type a player, club or screen name. With an empty query it lists your pending tasks ("Next step"). |
| Up / Down (in the palette) | Move the selection |
| Enter (in the palette) | Open the selected result |
| Esc (in the palette) | Close the palette |
| Ctrl+S | Save the career to its slot |
| F8 | Open Help: getting started, keyboard shortcuts and a glossary of football terms |
| Space or Enter | Continue: advance the calendar to your next match, or open the match on match day. Ignored while keyboard navigation has a widget focused, so it never doubles as "press this button". |
| Alt+Left | Back to the previous screen you were on |
| Alt+Right | Forward again to the screen Back left |
| Esc | Close the current screen: a profile, report or comparison returns to the screen beneath it, any other screen (Finances included) to Home. Does nothing on Home. |
| Tab / arrow keys | Keyboard navigation between widgets (Dear ImGui) |

### Back and Forward

Career screens keep a history like a web browser. Back returns to the
screen you were on before, whether you got there from the sidebar, a
shortcut, the palette or a link on a page; Forward re-opens what Back left.
Opening a new screen after going back drops the screens ahead, as in a
browser. The history holds the last 50 screens and starts afresh whenever a
career is started or loaded. Screens that can no longer be opened are
skipped: the profile of a player who has left the game, or one of your
club's screens while you are out of work. The live match and the club choice are not part
of the history, so Back never leaves a match or the career.

| Input | Back | Forward |
|-------|------|---------|
| Keyboard | Alt+Left | Alt+Right |
| Mouse side buttons | Button 4 (back) | Button 5 (forward) |
| Touchpad | Two-finger swipe to the right | Two-finger swipe to the left |
| Top bar | *Back* button | Arrow button next to it |

One swipe is one step; a round arrow slides in from the edge of the window
while you swipe and the step happens once it is fully out. The swipe is
ignored while you scroll up or down, over a list that scrolls sideways,
while a text field is being edited and while a dialog is open. The top-bar
buttons are greyed out when there is nothing to go back or forward to.

### Sidebar hubs

The sidebar has seven hubs. The hub you are working in lists its screens
underneath its name, with the current screen highlighted; click one to open
it. Clicking a hub, or pressing its F-key, opens the hub's first screen.
Right after a sidebar click or an F-key, Up and Down move between the
screens of that hub; a click on the page gives the arrow keys back to the
page. Counters (unread messages, new scout reports) appear next to their
screen and, added up, next to the hub.

On narrow windows the sidebar shrinks to icons: hovering a hub's icon shows
its screens in a small menu beside it, and the current hub's screens also
appear as tabs above the page.

| Key | Hub | Screens |
|-----|-----|----------------|
| F1 | Home | Home |
| F2 | Inbox | Inbox, News |
| F3 | Squad | Squad, Lineup, Tactics, Squad planner, Medical, Compare players, Under-21s |
| F4 | Training | Training, Planning |
| F5 | Matches | Fixtures & Results, Competitions, Calendar, Opposition, International, Call-ups (head coaches only), Data hub |
| F6 | Recruitment | Transfers, Scouting, Youth |
| F7 | Club | Club, Finances, Staff, Delegation, Manager, Career timeline, Awards, Records |

Every screen can also be opened with Ctrl+K. While you are out of work, only
the screens that make sense without a club are available (Home, Inbox,
News, Competitions, International, Manager, Career timeline, Awards and
Records, plus Call-ups if you coach a national team), and the F-key of a hub with none of them does
nothing.

### Mouse on management screens

| Where | Action |
|-------|--------|
| Team selection | Click a club to inspect it, double-click to start the career with it straight away |
| Squad list | Click a player to show his details in the side panel, double-click to open his profile (on narrow windows a single click opens the profile) |
| Scouting and search lists | Click to select a player, double-click to open his profile |
| Calendar | Click a day to select it; double-click a fixture to open its match report (played) or the opponent's club page (upcoming) |
| Lineup pitch | Click a starter to select him; drag an outfield player to move him on the pitch (the goalkeeper stays in goal); double-click for his profile |
| Lineup bench | Click a substitute to select him, double-click for his profile, or drag him onto a starter to swap them. With one starter and one substitute selected, *Swap selected players* confirms the change. |
| Top bar | *Back* returns to the previous screen and the arrow next to it goes forward again; the search field opens the palette; the holiday button next to Continue opens the holiday planner |

## Match day

The match screen has its own keys. They are ignored while you type in a text
field.

### Playback

| Key | Action |
|-----|--------|
| Space | Pause or resume the match (while the substitutions board and the tactics panel are closed) |
| . (period) / , (comma) | Next faster / next slower playback speed |
| V | Switch between the 2D and the 3D view (the same simulation, only the view changes) |
| S | Open or close the substitutions board |
| T | Open or close the tactics panel |
| F | Toggle pitch focus: the match view fills the window under a compact overlay |
| Esc | Close an open dialog or the analysis panel, then leave pitch focus |
| M | Mute or unmute the match sound |
| Alt+Enter | Toggle a full-screen window (match screen only; elsewhere use *Settings > Fullscreen*) |

Speed (1x, 2x, 4x, 8x, 16x, 30x), *Highlights*, *Quick result*, *Finish
match*, substitutions, tactics and the assistant options are buttons in the
match toolbar. At 1x one second of match time takes one real second. With
*Highlights* on, the game skips instantly to the next key moment and plays
that moment at the chosen speed. With *Settings > Pause at half-time* on,
your match stops at half-time (and at the breaks around extra time and
before a shootout) at any speed until you resume it.

### Substitutions board and tactics

| Input | Action |
|-------|--------|
| Drag a substitute onto a player | Plan a substitution; several planned changes are made together at the next stoppage (with the newer match engine, the players walk off and the subs walk on before play restarts) |
| *Pause while open* | Keeps the match paused while the board or the panel is open (a saved setting) |

The tactics panel changes the style, the instructions and the formation
during the match. A new formation costs some tactical familiarity, which
the team recovers over the following minutes; *Undo* restores the previous
tactics.

### Touchline shouts

Hold Shift and press a key of the number row. The keys follow the order of
the shouts bar; the bar shows the key your keyboard layout prints on each.

| Key | Shout |
|-----|-------|
| Shift+1 | Encourage |
| Shift+2 | Demand more |
| Shift+3 | Calm down |
| Shift+4 | Push up |
| Shift+5 | Drop deeper |
| Shift+6 | Press more |
| Shift+7 | Stand off |
| Shift+8 | Keep the ball |
| Shift+9 | Counter |
| Shift+0 | Into the box |
| Shift+- (the key after 0) | Shoot on sight |

A shout's effect fades over about ten minutes, and shouting again soon
afterwards has less effect.

### 3D cameras

| Key | Camera |
|-----|--------|
| 1 | Broadcast |
| 2 | Tactical |
| 3 | End (behind the goal) |
| 4 | Player follow |
| 5 | Free |
| 6 | TV director: cuts between angles, with close-ups after goals |
| B | Follow the ball while keeping your current angle (switches to the free camera; press again to stop following) |
| R | Reset the view |

### Mouse in the match view

| Input | Action |
|-------|--------|
| Left-drag | Orbit around the camera target. Dragging with any preset hands the view over to the free camera from where it is. |
| Right-drag, middle-drag or Shift+left-drag | Pan across the pitch |
| Mouse wheel | Zoom |
| Double-click (free camera) | Look at the clicked spot |
| Double-click (other cameras, or the 2D view) | Toggle pitch focus |

In the sandbox's 2D view, the mouse wheel zooms in as far as the pitch
window allows while keeping the entire pitch visible, including both goal
lines. In Watch mode, zoom is centred on the pitch. Scroll down to return
to the full stadium view. In Play mode, the view follows the action within
the space available around the pitch.
Double-click toggles pitch focus. Orbit and pan apply to the 3D view; outside
the sandbox, the 2D mouse controls only toggle pitch focus.

### Play mode

In your club's live match, *Play* (before kick-off) or *Take control* (while
you watch) lets you play the match yourself. The match pauses first and shows
the controls; *Start playing* hands you the team. You always control one
footballer of your side, the *active player*, marked by a yellow ring with
his name and condition above him; control moves between team-mates as the
play goes on. Everybody else, both teams' goalkeepers and every restart
(kick-offs, throw-ins, corners, free kicks, penalties) stay with the AI. A
played match runs in real time, with the same rules, attributes and fatigue
as a watched one: what you choose is where the ball should go, while the
player's technique, the pressure on him and how tired he is decide how well
he does it.

The stick and the movement keys follow the screen: up is up in the 2D view
and in the 3D play camera alike.

| Key | Gamepad (standard layout) | Action |
|-----|---------------------------|--------|
| W A S D or the arrow keys | Left stick | Move the active player |
| Shift | RT or RB | Sprint |
| J | A (bottom) | Pass to the team-mate nearest the direction you aim; when defending, standing tackle |
| K (hold) | B (right) | Shoot: the longer you hold, the harder the shot (past the mark on the bar it gets hard to keep down); when defending, slide tackle |
| L (hold) | Y (top) | Through ball into the space ahead of a team-mate's run; hold longer to play it further |
| I (hold) | X (left) | Lofted pass, or a cross from wide areas; hold longer to play it further |
| E (hold) | LT | Jockey: stay on your feet facing the ball at a contain pace; with no direction pressed he holds a goal-side line on the carrier |
| Q | LB | Switch to the team-mate shown with the white dashed ring; press again quickly to cycle to the next one |
| Esc or P | Start | Pause menu |

A pass, a shot or a tackle pressed a moment before the ball reaches the
player is played first time. The power bar appears under the player's name
while you hold a button; a tap plays a soft, deliberate ball.

**Pause menu**: *Resume*, *Tactics*, *Substitutions*, *Hand back to AI* and
the assistance options below. *Hand back to AI* is also a toolbar button,
available at any stoppage. While you play only the play keys are listened
to; the view, the cameras, tactics and substitutions are in the toolbar and
the pause menu. Speed, highlights and *Quick result* are hidden while you
play. Every play key except Shift (sprint) can be rebound in *Settings >
Controls*; the keys above are the defaults.

**Assistance** (pause menu, or *Settings*):

| Option | Choices |
|--------|---------|
| Player switching | *Manual*: only the ball carrier is taken automatically. *Assisted*: also the receiver of your pass, and the best placed defender once you let go of the stick. *Automatic* (default): control always follows the team-mate best placed to reach the ball. |
| Pass assistance | *None*: the pass goes along the stick. *Normal* (default): it leans toward the team-mate the ball is likelier to reach near your aim. *Strong*: it picks him in a wide cone. |
| Stick dead zone | How far the stick must move before the player does (0.05 to 0.50, default 0.20) |

Gamepads can be plugged in and out at any time; the first connected pad is
used. The 2D view keeps the whole pitch visible and follows the active
player and the ball within the space available. The 3D view uses the *Play*
camera, which keeps both in the picture. A radar of the whole pitch sits at
the bottom of the view. An arrow at the edge points to the active player
when he is out of the picture.

## Where your files are

Saves, settings, logs and screenshots never go next to the game. They live in
the user data directory:

| System | Folder |
|--------|--------|
| Linux | `$XDG_DATA_HOME/FlavioMili/FootballManagement` (usually `~/.local/share/FlavioMili/FootballManagement`) |
| macOS | `~/Library/Application Support/FlavioMili/FootballManagement` |
| Windows | `%APPDATA%\FlavioMili\FootballManagement` |

Saves are in `saves/`, the log and any crash reports in `logs/`, and
screenshots in `captures/`.
