# 3dmppc-editor — the application a game is authored in

A separate program from the console and the tools. It works on a game
directory, drives `mppcburner` to build it and a development console
(`3dmppc --dev`) to run it, and links none of their code.

It shows its tiled workspace between the menu bar and a status bar, on a
window of 1280x720 times the UI scale or bigger. A tile's header has X on
the left to close it and M on the right to maximize it, and a right click on
it opens the tile's menu; a tile can be split, closed, maximized or turned
into another kind of pane (Window > New Tile lists every kind), and several
panes in one tile show as folder tabs. Dragging a tile's header or a tab to
the edge of another tile,
or into its tab strip, shows where it would land before it is dropped there;
Escape cancels the drag and the tile stays where it was. Each kind of pane
keeps a minimum size of its own - a log pane room for its control row, the
Game its mode row and status line (the picture shrinks below 1x to fit, and
never grows past the tile), a strip of buttons its buttons - so a default layout's
controls stay visible even at 1280x720. The tile with the focus wears a brass
frame; Ctrl+F6 and Ctrl+Shift+F6 (Window > Focus Next / Previous Pane) move
the keyboard between tiles, as Tab belongs to the code editor and the
terminal.

Four layouts share the same panes, documents, build and runtime: Code, Scene,
Debug and Burn, chosen once a project is open, at the right end of the menu
bar or in Window > Reference Layouts. The first time a layout is chosen it
shows its standard tiles; after that it keeps whatever the user made of it -
sizes, new tiles,
tabs - saved on exit to its own file, `$XDG_CONFIG_HOME/3dmppc-editor/layout-code`,
`layout-scene`, `layout-debug` and `layout-burn` (`~/.config/...` without the
variable), and the one shown last is remembered. Choosing another layout changes
only what is shown, never a process, a build, a buffer or the log. Window >
Reset Layout puts the chosen layout back to its standard tiles; the others keep
theirs. A layout saved by an earlier editor in `layout` is read as Code's.

Without a project it shows the Project Catalog: a Project Toolchest on the
left with Recent Projects, New Project..., Open Project..., Settings... and
Help, and beside it the page chosen there. Recent Projects lists the projects
opened lately, each with its letter, name, location and Available or Missing
for whether its disc.toml is still there; the arrows or a click select one,
and Enter or a double click opens it, and it scrolls once the list outgrows
six rows. Its context menu has Remove from Recent, which only takes it off the
list and can be undone. Right under the list, in the same column, Selected
Project shows the whole path, whether disc.toml was found, and Open <name>; a
project that is gone offers Locate... instead. New Project is a page: the form
(template, name, disc id, directory) on the left, the path it will create and
its files on the right; a problem with the directory or a clash with an
existing one shows beside that field, and nothing is created until Create
Project. What is typed stays while other pages are shown; Reset clears it.
Open Project and every Browse... show the editor's own file browser inside the
page: what it is for, the path, Up, the entries and the action, with Cancel
keeping the field as it was. With a project, Settings, Help and Open Project
are tabs instead, opened in the biggest tile, and closing a tab brings back
the one that was in front before it.

With a project, the editor builds it with `mppcburner` and runs the result in a
development console that draws into the Game tile, driven over the console's
dev channel. Project > Project Settings groups what it shows: Disc for what
comes from disc.toml, Editor for the editor's own paths and tools. Settings
says where the tools are, in the order a disc goes through them: Baker,
Burner, Runtime and Player. Help groups the shortcuts by the menu that holds
each command, in the menu bar's order, then by the keys that work in a pane.

The editor has no modal windows: nothing darkens the window or has to be
answered before anything else works. A form, a setting or a question is a page
beside the Project Toolchest, a tab in a tile or an area inside the pane it is
about, and waiting for an answer holds only what asked. Context menus,
drop-down lists and tooltips stay: they are short-lived and close with Escape
or a click elsewhere.

## Building

On its own:

```sh
cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
./editor/build/3dmppc-editor
```

As part of the development kit, next to the development console and the tools:

```sh
cmake -S . -B build-dev -G Ninja -D3DMPPC_DEVTOOLS=ON && cmake --build build-dev
./build-dev/pconsole/3dmppc-editor
```

A player build of the console never builds it.

## Running

```sh
./editor/build/3dmppc-editor [PATH]
```

`PATH` is a game directory or its `disc.toml`; both open the same project.
File > Open Project... does the same from the menu, for a directory.

The editor draws one of its pixels per screen pixel, on a HiDPI display too.
View > UI Scale sets 1x, 1.5x or 2x and saves the choice; a scale the display
cannot hold at 1280x720 times that scale is greyed out with a tooltip naming
the display it needs; a saved scale the display cannot fit is lowered at
startup. The code text size and the Game scale are separate choices.

### Building and running a game

| Command | Key | What it does |
| --- | --- | --- |
| Build | Ctrl+B | `mppcburner build <root> --unpacked <cache>/builds/<n> --baker <mppcbaker> --map <cache>/builds/<n>.map` in the background |
| Run / Resume | F5 | `3dmppc --dev --frame-fd 3 --memcard <card> [profile options] <cache>/builds/<n>`; builds the saved files first when nothing has succeeded yet or one changed since; on a paused game, `resume` |
| Pause | F6 | `pause`, shown as Pausing until the console confirms it |
| Step Frame | F7 | `step`: one frame of a paused game |
| Stop | Shift+F5 | `quit`, then waits; Force Stop kills a console that does not end |
| Run Last Successful Build | | runs the older build after a later one failed or was cancelled |
| Build and Restart | | Build, then stop the running game and start the new build; a failed build leaves the old game running |

With a file unsaved, Build and Run wait in a Review Changes tab: Save and
Build / Build Saved Files / Return, and the same for Run. Run > Run
Configuration keeps the project's run profiles in `.3dmppc-editor/project.toml`,
grouped as the console sets them: Session first, with the runtime, working
directory, Start Paused, Fixed Step, Reload On Save, more console options and
environment variables, then Audio (ca) with Mute, and Memory Card (cm) with
the card path. It is a pane, brought forward as a tab by the menu and by the
Profile button of Runtime Controls: the profiles in a list with New (N) and
Delete (X) under it, the chosen one's fields beside it. The form says before
Run what the console would refuse; Apply is for the next Run, Apply and
Restart stops the session and runs again.

Every icon in the editor, on a button or beside a file, is a two-letter code
from one table, one code per thing, drawn in the same square: Bd Build, Rn Run,
Ps Pause, Sf Step Frame, Sp Stop, Rl Reload, Mo More, Pl Run in Player, Ex
Export, Nw New, Nd New Folder, Dl Delete, Mv Rename, Rf Refresh, Rc Recent, Op
Open, Se Settings, Hp Help, Mk Mark Moment, Cp Capture Frame, Is Report Issue,
Rs Restart; a file's code is Fd folder, Ln link, Lu Lua, Cc C/C++, Hh header,
Tm TOML, Mf disc.toml, Im image, Sn sound, Tx text, Fi anything else. The tile
frame's X and M stay letters.

A status reads as text, not a button: a mark before its label. [OK] means
done or fine, [*] going on (Running, Reloading), [~] busy (Building), [-]
idle, [!] warning, [X] error.

Runtime Controls is a strip of square buttons, a coloured code over a short
label: Build Bd, Run Rn, Pause Ps, Step Sf, Stop Sp, Reload Rl; the tooltip
gives the whole name and key. Reload is always there, dimmed with the reason
when the running disc has nothing to reload. Its target is the last Code
tile's file: the entry script, a Lua module, or a texture, else the entry
script; a texture is baked with the burner first and only the baked bytes are
sent, and a bake that fails is never sent. When the change needs a full
rebuild instead - a C++ file, a scene, a sound, disc.toml, or a kind Reload
does not know - Build and Restart takes Reload's place and says so, warning
that restarting loses the game's current state. Release Controls in Burn have
Build Bd, Run Rn, Player Pl, Stop Sp and Report Ex. The Game tile of a layout
as it starts shows the disc's screen at Fit with no border, until a splitter
is dragged.

Each build goes to a new numbered directory and counts only when the burner
exits 0; a failed or cancelled build is deleted and never runs. A running
game keeps running while the next build is made. Console Output shows the
editor's, the build's, the candidate's and the runtime's lines together;
the Runtime Log keeps to the runtime alone and the Build Log to candidate
builds alone, until more are ticked. The Runtime Log's Source menu also
narrows to one run at a time, All runs being the default; a row's tooltip
names the process, the run and whether the line is stdout or stderr. The dev
channel's own lines are there too, off by default, without the frame events
the console sends sixty times a second. Every log pane's controls draw in the
interface font on one row - Source, Level, Find, Follow, Wrap, Copy, Export
and Clear View - and what does not fit goes behind a labelled More; only the
log lines themselves keep the code font. The tile's title names what the
filters keep. Time is the local clock time a line arrived. The borders
between Time, Level, Source and Message drag; a click selects a line,
Shift+click a range, and Ctrl+C or Copy copies them (Copy takes every line
shown when none is selected). Copy and Export both write the same line: its
source, process and stream, as well as the time, level and text. A console
that stops reading its input gets at most 1 MiB of queued commands; past that
a command is refused and Console Output says so once, and Stop then offers
Force Stop. Closing the editor stops the build and the game it started.

The editor speaks the PDK version as its protocol and refuses a console of
any other version, with the reason, a player build of the console included.

### Code

Files on the left, the code tile beside the Game over its Runtime Controls,
and under both one tile of Console Output, Problems, Terminal and Search
Results, Console Output in front. Edit > Find in Project and a build's
diagnostics bring their tab forward.

- **Problems** lists the source (the build, or a language server by name), the
  file, line and column, and the message of each error and warning; a double
  click or Enter opens the file at that line and column, and a row whose file
  is gone or changed since that build says so. The full build output stays in
  the Build Log.
- **Search Results** (Ctrl+Shift+F, Edit > Find in Project) searches the
  project's text files a line at a time and lists file, line and text; dot
  folders and `build*` are left out unless ticked, and the result says where it
  did not look.

### Debug

For playing the game and finding out what it does, the session in front:
Runtime Controls along the top, the Game beside the Session Toolchest over
Session, Inspector and Test Case, the Runtime Log and Findings along the
bottom. Code, Files and the Terminal open from Window > New Tile, or where
a file is opened. The tiles are not fitted to the Game here: Fit draws the
whole frame in whatever the Game gets. The transport keeps one row: what does
not fit goes behind a labelled More. Session says the session's state once;
the Game and Runtime Log titles and the Runtime Controls strip do not repeat it.

- **Session** says what ran: its number and state, the build, the run profile,
  when it started and for how long, the frame, the disc, the
  entry script's revision and the last reload, and the marks made in it. After
  the console ends, how it ended comes first. Details, collapsed, holds the
  build's directory and the code hash, each with its own Copy.
- **Session Toolchest**, square buttons as Runtime Controls draws its own: Mark
  Moment writes the frame and a word into the log and into
  Session while the game goes on; Capture Frame saves the Game's frame into the
  findings; Report Issue captures the frame, gives the keyboard back and brings
  Findings forward; Restart stops the session and runs the same profile again.
- **Test Case** lists the project's `testcases/*.txt` (`title:` on the first line,
  then `steps:` and `expected:` each over its text) and Exploratory, for free play.
  Passed, Failed and Blocked save the result with a note and the session into the
  findings; Failed also captures the frame and starts a finding from the case.
  New Test Case writes `testcases/case-N.txt` and opens it in Code.

- **Inspector** shows what the console says about itself in its status: session,
  frame, disc, code hash, PDK, the entry script's revision and whether a reload
  changed it, Lua memory. On a disc with a Lua machine it lists the persistent
  state with `keys` and reads pinned values with `get`, read-only. Each value
  says when it is true: `frame N` for a read on a paused machine (the console
  answers in order, so nothing ran in between), `sampled <time>, running`
  otherwise; the reads are never one snapshot. A paused machine is read again
  after every Step. A disc without a Lua machine says there is no state to
  inspect.
- **Reload** (F8, in the Run menu named for its target) sends `reload entry`
  for the entry script or `reload module` for a loaded Lua module by name;
  Runtime Controls shows it dimmed with the reason when no session runs or the
  disc has nothing to reload, and otherwise whether the last one was accepted,
  at which revision, or refused and why. A profile with Reload On Save queues
  a reload, one file at a time, when a file the console can apply is saved.
- **Findings**: Capture Frame writes the frame the Game shows as a PNG; Record
  Finding writes a title, steps, expected and actual behaviour with the session,
  frame, build, runtime, disc, code hash and entry revision, and the log since
  the session started.

### Burn

For checking one disc image before it is released. Release Controls (Build
Candidate, Run Candidate, Run in Player, Stop, Export Report) sit over the
Release Candidate beside the Candidate Playtest, with the Build Log and the
Playtest Log underneath; Checks, Findings and Build Result open from Window >
New Tile. Stop ends whatever session is running, a candidate's or a
development one, not only a candidate's. The Build Log shows candidate builds
(Source: candidate) by default; its Source filter brings the editor's, other
builds' and the runtime's lines back.

- **Build Candidate** runs `mppcburner build <root> -o <cache>/candidates/<n>.mppcdisc
  --baker <mppcbaker>`: a new number each time, never written over. A failed
  build makes no candidate and says so; the older ones stay. A candidate build
  logs its lines with their own source, candidate, kept apart from an ordinary
  build's.
- **Build Result** says the last build job's outcome, its process and exit,
  and its errors and warnings; Open Build Log opens that job's own log file,
  dimmed when the build kept none.
- A candidate shows its image, SHA-256, size, build time, command, tool
  versions, the git commit of the sources and whether they differed from it,
  and whether a project file changed since its build started.
- **Run Candidate** runs that image on the development console with a memory
  card of its own, `<n>.mppccard`; the unpacked development build never stands
  in for it. Candidate Playtest shows only that image, never a development
  session; while none of this candidate's own is running it says so instead of
  showing another session's frame, and starting a different session closes
  this one's playtest.
- **Run in Player** plays it in the player's own window: the 3dmppc of
  File > Settings built without devtools. Its output goes to the log and to a
  file beside the record; the Game tile names the candidate, the PID and the
  time.
- Eight checks, all required: Build, Disc loads and Player are set by the
  editor (the burner exited 0; the console mounted the image and answered its
  status; the player ended by itself with exit code 0 - Stop Player is the
  operator's act and leaves it Not run), Launch, Input, Audio, Main scenario
  and Exit by the operator after playing it. Each is Not run, Running, Passed, Failed, Blocked or Skipped, and
  Skipped does not count. Exit cannot pass after a run that was killed,
  crashed or force-stopped. They cover only what each says, not the whole game.
- Results belong to the image's SHA-256. Verify Bytes, Run Candidate and Approve
  read the image again; different bytes make every result and the decision void.
- Build succeeded, the checks passed and the decision are three separate
  lines: Not ready, Awaiting approval, or Approved or Rejected by the operator
  (`$USER`) at a time for a hash. Approve needs every check passed on the
  verified bytes; Reject and Undecide are always there.
- **Export Report** writes `<cache>/candidates/<n>-report-<time>.txt` with the
  candidate, the checks, the decision and the build and playtest logs. It sends
  nothing anywhere.

Each candidate's record, its checks and decision, and its build, playtest and
player logs are kept beside its image and come back when the project opens;
the bytes are hashed again then, and a check the closed window left Running
comes back Not run.

### Scene

A scene is `scenes/<name>.scene.toml`, a PDK 0.4 file: a 0.3 console does not
read it. It holds groups, cameras, meshes, quads, billboards and volumes, each
with a stable id, a name, a parent, a position, a rotation (degrees, yaw then
pitch then roll) and a scale. A mesh names a `.obj` as the disc names it, or
draws as a unit cube while its mesh is empty. A quad is a textured rectangle
in its own XY plane, sized from its scale; a billboard is the same card
always turned to face the camera; both carry a texture named as the disc
names it, a uv rect (`u0, v0, u1, v1` in texture pixels) and a tint. A volume
is a box whose meaning is up to the game that reads its own keys on it. A
mesh's `.obj` and a quad's or billboard's texture are disc asset names with no
folders: the editor finds the `.obj` among the project's `[assets]` files by
file name and the texture's picture among the `[textures]` sources by stem
(`name.mppctex` matches `name.png`); one that does not resolve draws as the
placeholder cube or a flat rectangle, and the viewport's status line says why.
The Scene layout opens the project's first scene; New Scene asks for a name
(a free one offered first) and, only for a C++ disc, whether to also write
`src/<id>_scene.hpp` (off by default; `<id>` is the name with non-identifier
characters turned to `_`). The header is the user's: the editor writes it once
and never over an existing file. New Scene also puts `scenes/*.scene.toml` in
disc.toml's `[assets]` when no pattern there already covers the new scene,
then opens, saves and edits it. Keys and sections the editor does not know
are written back as read, and a scene of an incompatible PDK version opens
read-only.
Its standard tiles put the Transform Toolchest over the Hierarchy on the left,
the scene in the centre, the Game over Runtime Controls over the Inspector on
the right, and Assets, Files and Console Output as tabs along the full width
at the bottom, Assets taking about two fifths of the height there.

- **Hierarchy**, its right-click menu and Scene > Add all create a Group,
  Camera, Box, Quad, Billboard or Volume; dragging an object onto another
  moves it there keeping where it is in the scene, or says why it cannot;
  Move to Root (Keep Local Values) is the other meaning.
- **Inspector** edits the selected object; a mesh's and a quad's or
  billboard's texture are typed in by name, as the disc names them, and a
  quad's or billboard's uv, tint and tess are typed or dragged in.
- **Assets** shows the resource files as an Icon Catalog or a list with the
  disc names of the last build's map. A double click opens a PNG or a sound
  (`.wav`, `.pcm`) as a tab beside the scene in the Scene tile, a
  `*.scene.toml` as the open scene (Hierarchy and Inspector follow it), and
  any other file in a new Code tab after switching to the Code layout.
  Selecting an asset shows a preview strip: a PNG whole with its pixel size
  and disc texture, a sound's length and disc name with Play/Stop (a WAV in
  its own format, a `.pcm` as the console's mono 44100 Hz samples; picking
  another asset stops it), and another file's kind and size. A selected file
  disc.toml does not list offers Add to disc, putting it in the section its
  kind belongs to (a PNG in `[textures]`, a WAV in `[sounds]`, anything else
  in `[assets]`) and changing only that list, keeping the file's comments
  and layout.
- **The Scene tile** holds the scene as its first tab; a PNG or a sound
  opened from Assets is a further tab beside it, closed from its own
  right-click menu. A picture tab shows it fitted with its pixel size and
  path; a sound tab shows its length, Play/Stop and a play error; closing a
  playing sound's tab stops it. The tab strip is shown only while such a tab
  is open.
- **The viewport** draws through an editor camera, never the game's, in one of
  three modes on its shelf: Wireframe (edges only), Filled (shaded polygons,
  painted far to near) and Textured (quads, billboards and meshes shown with
  their own texture, uv and tint; an untextured one flat). A camera, a group
  and a volume always draw as their own marker, in every mode: a camera as a
  body with a lens, a group as corner brackets with an axis cross, a volume
  as its box, never filled. A click selects, Move/Rotate/Scale (W, E, R; Q selects)
  drag the selection with Snap as the Transform Toolchest sets it, the right
  button orbits, the middle pans, the wheel dollies, and the labelled Rot X,
  Rot Y and Dolly thumbwheels do the same (a double click goes home). Persp,
  Top, Front, Right, Frame Selection (F), View All, Home and Seek set the view.

Every change is one undo step, one drag included, and Escape takes a drag back;
Ctrl+Z, Ctrl+Shift+Z, Delete and Ctrl+D work in the scene's tiles, and the
Scene menu saves, undoes and redoes. An unsaved scene
takes part in Save All and in the questions before Build, Run, Quit and Open.
Opening another scene while the open one has unsaved changes - from Open
Scene, the Scene tile's own Open buttons or a double click in Assets - is
refused with a note above the viewport, saying to save or undo them first;
New Scene refuses the same way, in its dialog. Opening the same scene again
keeps its edits.
A game shows a scene only if its code reads it: example-cpp ships
`scenes/main.scene.toml` through `[assets]` and draws each box with
`pdklib/rv_scene`. A changed scene needs Save, Build and a restart. Below the
viewport, a status line reads "Game: not running", "Game: read this scene" or
"Game: has not read this scene", from the scene names the running disc's own
development console has reported opened; its tooltip says to restart after
Save and Build, or names the open scene the running disc did not open.

The viewport shows `.obj` meshes, quads, billboards and volumes, but a disc
draws only what its own code draws. example-cpp, and a New Project made from
it, draws every mesh object as a unit box, ignores its mesh file, and does not
draw quads, billboards or volumes. Drawing `.obj` meshes, quads, polygons and
the rest in the game comes in the next version.

### Game

The Game tile shows the console's own frame. The editor makes a shared memory
object and hands it to the console as `--frame-fd`; the console writes each
finished frame there instead of opening a window, and the tile draws it without
smoothing at the size its Fit, Integer, 1x, 2x and 3x buttons (or View > Game
Scale) choose: Fit, the default, is the largest size that keeps the frame's
proportions, fractional scales included; Integer the largest whole multiple;
a fixed multiple that does not fit is lowered to one that does, and the status
line says so. What the frame leaves over is dark. A click on the picture gives
the game the keyboard,
with the console's own keys (arrows, Space or Z, X, C, V, Q, E, Tab, Esc);
Shift+Esc, a click elsewhere, closing or hiding the Game tile, another window
taking the keyboard, opening another project and starting a new console take it
back, and the game gets every key up at once. Paused, the tile keeps the last
frame as the disc drew it and says paused; after the session ends, the last
frame stays dimmed and marked stale. Sound still comes from the console.

### Files

The Files pane shows the project directory as it is on disk, reading a
directory only when it is opened, and follows changes made by any program,
including a save through a temporary file and a rename. New File, New Folder,
Rename and Delete work inside the project only: a new file never replaces an
existing one, a rename never lands on an existing name, and deleting a link
removes the link, not what it points to. Each asks inside the Files pane, above
the tree; Delete lists what it removes first. Links are shown with `->` and
never entered. The burner's `.mppcburn/` and `.git/` are not shown. A change to
`disc.toml` is read back into the Project pane at once; a running console keeps
the manifest it started with.

### Code

A Code tile is a window of one `nvim --embed` the editor starts with the first
Code tile, using its own config, [`nvim/rv_editor_init.lua`](nvim/rv_editor_init.lua),
not your `init.lua`. nvim must be on `PATH`; without it only the Code tiles say
so. A double click in Files opens the file in the focused Code tile, else in
the Code tile used last, else in a new one. A binary file (a NUL in its first
8 KiB) is not opened: Console Output says so, and Files > Open as Text opens
it anyway.

A Code tile keeps the files it has shown as tabs above the text: a click on a
tab shows that file again, opening a file that already has a tab brings the tab
forward, and a right click offers Close Tab (the file stays loaded; an unsaved
one keeps its tab). Split Right or Split Down on a Code tile makes a second Code
tile, another nvim window on the same file.

It starts as an ordinary editor: typing inserts, Shift+arrows select, Ctrl+S
saves, Ctrl+Shift+S saves all, Ctrl+Z / Ctrl+Shift+Z undo and redo, Ctrl+C,
Ctrl+X, Ctrl+V use the system clipboard, Ctrl+F searches, Ctrl+H replaces,
Ctrl+G opens nvim's command line. F2 switches to plain Vim and back. C and
C++ indent with 4 spaces and a ruler at column 129, everything else with 2
spaces; the wheel scrolls, and the cursor takes nvim's shape for each mode. F5, F6, F7, Shift+F5 and Ctrl+B
stay the editor's own while a Code tile has the keyboard.

A Code tile's header and status line name the file and mark it `[+]` while
unsaved; a new one is Untitled. A click puts the cursor where it lands, a drag
selects. Closing a tile whose file is unsaved and shown nowhere else asks inside
that tile: Save, Discard or Keep Open; closing the editor or opening another
project opens a Review Changes tab with Save All, Discard All or Return for
every unsaved file, and the editor stays usable meanwhile. A file counts as
saved only when nvim reports the write: one it could not write stays unsaved
and open, listed
with nvim's reason, and nothing closes until each is saved or the user says
Discard. An Untitled file gets a name through Save As, here or in File, which
never replaces an existing file. A file changed by another program is re-read
when its buffer is clean; when it is not, nvim asks.

nvim's own LSP client connects clangd (C and C++, from `.mppcburn/`'s
`compile_commands.json`) and lua-language-server (Lua): diagnostics feed
Problems, Ctrl+K shows hover and F12 goes to a definition. A Code tile's
status line names a missing or stopped server, with the reason in its
tooltip. While a session is running, the status line also says what
saving the shown file would do to it - reload the entry
script or a module, refresh a texture, or Build and Restart - from the same
plan Reload uses.

### Terminal

A Terminal tile runs your `$SHELL` (else `/bin/sh`) on a pseudo-terminal of its
own, in the project's directory, with `TERM=xterm-256color`. The screen is kept
by [libvterm](../third_party/libvterm/ORIGIN.md) and drawn in the code font on
the editor's code palette. Ctrl+C interrupts, the terminal takes the tile's size, the
wheel scrolls back through up to 5000 lines and typing returns to the bottom.
F5, F6, F7 and Ctrl+B stay the editor's own. It is a session of its own that
never sees the console's dev channel. When the shell ends the tile says how and
offers Start Again. Closing a tile whose shell still runs asks inside the tile,
End Shell or Keep; ending it hangs up the shell and ends everything started in
it, background jobs included. A paste of several lines shows them over the
screen first, with Paste and Cancel. Every Terminal tile is its
own shell.

### Where things go

| What | Where |
| --- | --- |
| Layouts | `$XDG_CONFIG_HOME/3dmppc-editor/layout-code`, `layout-scene`, `layout-debug`, `layout-burn` |
| Settings | `$XDG_CONFIG_HOME/3dmppc-editor/settings.toml` |
| Code text size, Game scale, layout shown | `$XDG_CONFIG_HOME/3dmppc-editor/view` |
| Builds | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/builds/<n>` |
| A build's own log | beside it, `<n>.log` |
| A session's own log | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/sessions/<started, YYYYmmdd-HHMMSS>-<session number>-<pid>.log`, the 20 newest kept |
| A baked texture staged for Reload | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/staging/<build number>/<name>` |
| Memory card | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/memcard.mppccard` |
| Findings, captured frames, test results | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/findings/` |
| Release candidates: images, records, logs, memory cards, reports | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/candidates/` |
| Burner maps of builds and images | beside each, `<n>.map`, `<n>.mppcdisc.map` |
| Recent projects | `$XDG_CONFIG_HOME/3dmppc-editor/recent` |
| Run profiles | `<project>/.3dmppc-editor/project.toml` |
| Scenes | `<project>/scenes/*.scene.toml` |

Without the variables: `~/.config`, `~/.cache`, `~/.local/state`. The game
directory gets only its scenes, the run profiles and what `mppcburner` itself
leaves there (`.mppcburn/`).

The console, the burner and the baker are looked for next to the editor's own
executable, which is where a development build puts all four
(`build-dev/pconsole/`). `settings.toml` overrides any of them, in the same
dialect as `disc.toml`:

```toml
[tools]
console = "/path/to/3dmppc"
burner = "/path/to/mppcburner"
baker = "/path/to/mppcbaker"
player = "/path/to/3dmppc"   # built without devtools; there is no default
```

Settings (File > Settings, or the Project Toolchest without a project) edits
`[tools]` on Apply and keeps the rest of the file; for each tool it shows the
override, the path in use and its status, with Browse and Check. A missing tool
disables only the commands that need it, with the reason; the Project pane
shows each path and the version the tool reports (`mppcburner --version`).

View > Widget Catalog shows every widget in the specification's twelve
sections, drawn by the editor's own code on fixed data.

### Fonts

The interface draws in pdklib's 5x7 bitmap font (`rv_font`, 8 px high, one
blank column between letters) with no magnification beyond the UI scale. Code
tiles and the terminal draw fully in PxPlus IBM VGA 9x16 by VileR (int10h.org);
in Console Output only the lines match it, its own controls stay in the
interface font. View > Code Text Size picks Normal (16 px, the default), Large
(32 px) or Small (PxPlus IBM EGA 8x14 at 14 px), each times the UI scale, and
the choice is kept. The two font files and the Cyrillic of the 5x7 font carry
PxPlus's CC BY-SA 4.0 licence:
see
[`third_party/pxplus-ibm-vga/`](../third_party/pxplus-ibm-vga/ORIGIN.md).

## Layout

| Path | What |
| --- | --- |
| `src/` | the editor's sources |
