# 3dmppc editor

## What this editor is

A separate program from the console and tools. It works on a game directory,
drives mppcburner to build it and a development console to run it. It works
at 1280x720 times the UI scale or bigger, showing tiled panes of code, logs,
game view and settings between the menu bar and a status bar.

## Workspace: tiles and layout

Tiles hold panes (code, logs, game view, files, etc.). A tile's header has
X on the left to close and M on the right to maximize; right click opens the
menu. Tiles can be split, closed, maximized or changed to another kind.
Several panes in one tile show as folder tabs. Drag a tile header or tab to
another tile's edge or tab strip to move it there. Escape cancels. The
tile with focus wears a brass frame. Ctrl+F6 and Ctrl+Shift+F6 move the
keyboard between tiles.

## Four layouts: Code, Scene, Debug, Burn

Four layouts share the same panes, documents, build and runtime. Choose one
once a project is open, at the right end of the menu bar or in Window >
Reference Layouts. Each layout keeps its own arrangement of tiles and
windows, saved to ~/.config/3dmppc-editor/layout-code, layout-scene,
layout-debug and layout-burn (or XDG_CONFIG_HOME if set). The last one
chosen is remembered. Reset Layout puts the chosen layout back to its
standard tiles without changing the others. Choosing another layout changes
only what is shown, never what is built, running or being edited.

## Project Catalog: without a project

The Project Toolchest on the left shows Recent Projects, New Project,
Open Project, Settings and Help. Recent Projects lists your recent games
with their status. Select one and press Enter or double click to open it.
New Project is a form for a template, name, disc id and directory. Open
Project opens a game directory. The Toolchest's pages answer from there.
New Project text stays while you look at other pages; Reset clears it.

## No modal windows

The editor has no modal windows. A form, setting or question is a page
beside the Project Toolchest, a tab in a tile or an area inside the pane.
Context menus, drop-downs and tooltips stay short-lived and close with
Escape or a click elsewhere.

## Building and running a game

With a project open, click Build (Ctrl+B) or Run (F5). Run builds the
saved files first if nothing has succeeded yet or one changed since.
Pause (F6), Step Frame (F7) and Stop (Shift+F5) control a paused or
running game. Reload (F8) updates a texture or Lua script in a running
session without a rebuild. Build and Restart stops the game, builds again
and starts the new build. The Game tile shows the disc's screen; Console
Output, Problems, Runtime Log and Build Log hold the details. Run >
Run Configuration keeps profiles in .3dmppc-editor/project.toml,
grouped as the console sets them: Session (runtime, working directory,
Start Paused, Fixed Step, Reload On Save, more options, environment),
Audio (Mute), Memory Card (card path). Every build goes to a new
numbered directory and runs only if the burner exits 0. A running game
keeps running while the next build is made. Each log pane's controls show
Source, Level, Find, Follow, Wrap, Copy, Export and Clear View in one row;
buttons that do not fit go behind More. Every icon in the editor is a
two-letter code in the same square: Bd Build, Rn Run, Ps Pause, Sf Step Frame,
Sp Stop, Rl Reload, and others for the panes and files.

## Code

The Code layout shows the source files on the left, the code editor and game
view in the middle, with build output below. Open the project folder to browse
and edit files. Build and Run commands sit above the game frame. If the build
finds errors, the Problems pane lists them with line and column; click to open
and fix. Search Results (Ctrl+Shift+F) finds text across the whole project.

## Debug

The Debug layout puts the game in front for playing and watching what happens.
Runtime Controls at the top let you Pause, Step Frame and Run. Session shows
the game's state and marks you make while it plays. Inspector reads what the
game's code reports, frame by frame when paused. Test Case lists test scenarios
in the project; mark each as Passed, Failed or Blocked with a note. Findings
stores frames you capture during play.

## Burn

The Burn layout is for checking a release candidate disc before shipping.
Build Candidate makes a numbered image file. Run Candidate plays that exact
image with its own memory card. Run in Player opens it in the player program.
Eight checks mark off as you test: Build, Disc loads, Player, Launch, Input,
Audio, Main scenario, Exit. Approved or Rejected seals the decision once all
checks pass. Export Report writes the test log and results to a file.

## Scene

The Scene layout edits 3D scenes. Each scene is a set of objects with position,
rotation and scale. The Hierarchy on the left lists Groups, Cameras, Boxes,
Quads, Billboards and Volumes. Inspector edits the chosen object's transform
and texture. The viewport shows the scene in Wireframe, Filled or Textured mode.
Assets at the bottom opens your project's textures, sounds and scene files.
Drag objects in the viewport or use the Transform Toolchest: W to move, E to
rotate, R to scale.

## Game

The Game tile draws the frame from the running game. Click on it to give the
game the keyboard; Shift+Esc takes it back. Use Fit, Integer or 1x, 2x, 3x
buttons to choose the window size. Fit scales to the tile size; Integer uses
whole multiples of the frame size. When paused, the last frame stays on screen.
Sound still plays when the window is hidden.

## Files

The Files pane shows your project directory as it is on disk. Open folders to
browse. Right click to New File, New Folder, Rename or Delete. Double click a
file to open it in the Code editor. Links show with an arrow and are not entered.
Folders named .mppcburn and .git are hidden.

## Code

A Code tile is a text editor window. Double click a file in Files to open it
here. Each file is a tab; click a tab to switch. Type, select with Shift+arrow,
cut and paste with Ctrl+X, Ctrl+C, Ctrl+V. Ctrl+Z and Ctrl+Shift+Z undo and
redo. Ctrl+S saves. Ctrl+F searches, Ctrl+H replaces. F2 switches Vim mode on
and off. The status line marks unsaved files with a plus sign and shows the
file name.

## Terminal

The Terminal tile runs a shell in your project directory. Type commands as you
would in a terminal window. Ctrl+C stops a running command. The wheel scrolls
back through 5000 lines of history. F5, F6, F7 and Ctrl+B stay available for
the editor. When the shell ends, the tile offers to start a new one.

## Where things go

The editor stores files in standard directories. Set XDG_CONFIG_HOME, XDG_CACHE_HOME
or XDG_STATE_HOME to use custom paths; otherwise ~/.config, ~/.cache and
~/.local/state are used. Per-project folders below are named by a hash of the
project path, shown as <hash>.
- Layouts in ~/.config/3dmppc-editor/: layout-code, layout-scene, layout-debug, layout-burn.
- Settings in ~/.config/3dmppc-editor/settings.toml.
- Code text size, Game scale, last layout shown in ~/.config/3dmppc-editor/view.
- Recent projects list in ~/.config/3dmppc-editor/recent.
- Builds per project in ~/.cache/3dmppc-editor/<hash>/builds/<n> with logs <n>.log.
- Baked textures staged for Reload in ~/.cache/3dmppc-editor/<hash>/staging/<build number>/<name>.
- Release candidates in ~/.cache/3dmppc-editor/<hash>/candidates/.
- Burner maps beside builds and images: <n>.map and <n>.mppcdisc.map.
- Session logs in ~/.local/state/3dmppc-editor/<hash>/sessions/, named
  <start time YYYYmmdd-HHMMSS>-<session number>-<pid>.log, the 20 newest kept.
- Memory card in ~/.local/state/3dmppc-editor/<hash>/memcard.mppccard.
- Findings, captured frames and test results in ~/.local/state/3dmppc-editor/<hash>/findings/.
- Run profiles in the project folder: <project>/.3dmppc-editor/project.toml.
- Scenes in the project folder: <project>/scenes/.
- Burner output in the project folder: <project>/.mppcburn/.

## Tools and Settings

The console, burner and baker are looked for next to the editor's own
executable. Settings (File > Settings, or Settings in the Project Toolchest
without a project) shows each tool's override, the path in use and its status,
with Browse and Check buttons. Apply saves the changes. A missing tool disables
only the commands that need it, with a reason shown.

## Fonts

Code tiles and the terminal use a 9x16 monospace font. View > Code Text Size
picks Normal (16 px, the default), Large (32 px), or Small (14 px), each times
the UI scale; the choice is kept. The editor's interface draws in a small
bitmap font that scales with the UI scale.

## Help and this manual

This manual is open from Help > Manual in the menu bar or by pressing F1. A
link to it appears on the editor's start page, where you see recent projects.
