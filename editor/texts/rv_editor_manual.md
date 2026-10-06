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
two-letter code in the same square: Bd Build, Rn Run, Ps Pause, Sf Step,
Sp Stop, Rl Reload, and others for the panes and files.
