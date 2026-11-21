# 3dmppc editor

## What you need

The editor builds and runs games for the 3dmppc console. It works on a game directory: a folder with a disc.toml in it. The editor is a separate program, and it drives the tools next to it.

- the console, built with devtools, to run the game
- mppcburner, to build the game
- mppcinspector, to check the sources before a build
- the bakers mppctexbaker, mppcmusbaker and mppcmodbaker, for PNG, WAV and glTF files
- a player, 3dmppc built without devtools, only for a candidate's last check

A development build puts all of them but the player beside the editor, so you need to set nothing up. The window needs 1280x720 times the UI scale or more.

## First start: open or create a project

Without a project the Project Toolchest on the left shows Recent Projects, New Project, Open Project, Settings and Help.

To open a project, choose File > Open Project... or Open Project in the Toolchest, and pick the game directory. To open one you used lately, select it in Recent Projects and press Enter or double click.

To start a new game, choose New Project, pick a template (Demo (C++) or Demo (Lua)), give a Name and a Disc ID, choose the Directory and press Create Project. Reset clears the form.

The editor never opens a modal window. A form, a question or a setting is a page next to the Toolchest, a tab in a tile or an area inside a pane.

## Build and run

To build, choose Project > Build or press Ctrl+B. To cancel it, choose Project > Cancel Build.

To play, press F5 or choose Run > Run. Run builds first if the last build did not succeed or a file changed since, then starts the game. A build uses the files as saved, so if a file has unsaved changes the editor asks first. The picture appears in the Game tile.

To pause, press F6. To run one frame of a paused game, press F7. To stop, press Shift+F5.

To change a Lua script or a texture in a running game without a rebuild, save it and press F8. Choose Run > Build and Restart when a change needs a new build.

To run the last build that succeeded with your current scripts and assets, choose Run > Run Last Successful Build.

To change how Run starts the game, choose Run > Run Configuration... Profiles are kept in the project folder. They hold the runtime, the working directory, Start Paused, Fixed Step, Reload On Save, Mute and the Memory card.

While a build runs, the status bar and Runtime Controls show its steps in a compact strip, for example [v][v][>][ ][ ] compile: one bracket for each step and then the name of the running step. The mark in a bracket is v for done, > for running, a space for waiting, x for failed and ! for interrupted. The same strip is in the status bar after a build that failed.

Every build goes to a new numbered directory. Run and Build and Restart start the game only if the burner ends with exit code 0. A running game keeps running while the next build is made.

## When a build is refused

Before it builds, mppcburner runs mppcinspector on the disc. Without mppcinspector in the bakers' directory the editor does not start a build at all. If the inspector finds an error in the sources, the build is refused and nothing is compiled.

A build that fails or is refused opens Inspector Report in front, with its tile focused. Inspector Report is a tab beside the logs in every layout: Console Output in Code, Assets in Scene, Runtime Log in Debug and Build Log in Burn. It is never in the Runtime Controls or Release Controls strip. At the bottom of it one strip says Build did not start, when the build was refused before it began, or Build #N failed, with the reason and an OK button on the same row. When the inspector's errors stopped the build, the reason is "the sources have K error findings, so the build does not accept them". The strip is there only while there is something to say, and the findings above it do not move; a long reason scrolls inside the strip. It stays until you press OK or start the next build.

To see what is wrong, open Inspector Report. Its table has three columns: an icon, File:line and Message. The icon is the level; hover it to see the level and the source, inspector or build. The message ends with the rule and the key in brackets and wraps inside its column, so there is no horizontal bar. Inspector Report also shows warnings, and it fills up again when you open the project, when you save a file and after every build. The tab shows the count, Inspector Report (N).

To jump to the place, double click the row or press Enter on it. The file opens at that line.

The burner prints each finding as one line. For example, if disc.toml has files = ["assets/snd/*.wave"] under [assets] and no file ends in .wave, the build stops with this line.

disc.toml:11: error: pattern 'assets/snd/*.wave' matched no files under '.' [disc-toml-patterns, key [assets] files]

Read it from left to right. disc.toml:11 is the file and the line. error means the build is refused; a warning would not stop it. The text says what is wrong: the pattern matches nothing. The brackets name the rule, disc-toml-patterns, and the key, [assets] files.

To fix it, edit that line of disc.toml so the pattern matches real files, for example assets/snd/*.wav, or add the files it should find. Press Ctrl+S, then Ctrl+B. The row leaves Inspector Report when the inspector runs again and finds nothing.

Compiler errors and warnings from the build are in Inspector Report too, with the source build. The whole output is in Build Log.

## Layouts and panes

There are four layouts: Code, Scene, Debug and Burn. To switch, click one at the right end of the menu bar or choose Window > Reference Layouts. Each keeps its own tiles. Window > Reset Layout puts the chosen one back to its standard tiles. Switching changes only what is shown, never what is built, running or edited.

A tile holds one pane or several as tabs. To move a pane, drag its tile header or tab to the edge of another tile; Escape cancels. To close a tile use the X in its header, and to maximize it use the M. Right click the header to split, change or close it. To add a pane, choose Window > New Tile. To move the keyboard to the next tile, press Ctrl+F6. When the tabs of a tile do not fit, the row gets the arrows < and > and a list v of all the tabs; the mouse wheel over the row also scrolls it, the open tab always stays in view and there is no scrollbar under the tabs. A single tab is still shown, and a tab title is shown whole.

A tile does not change its look by state: buttons that cannot be used are dimmed and say why in their tooltip. A tile cannot be made smaller than the size at which everything in it is shown whole. Names, paths and messages are never cut with ...; a long one scrolls inside its list or text area, and a scrollbar shows only when the content really does not fit. A path inside the project is shown relative to the project.

Message strip. A message of a pane, an error or a short question appears as a strip at the bottom of the pane, under its list, with its buttons on the same row, on the right. With nothing to say there is no strip and no empty place. When it appears nothing above it moves: the shelf, the column headers and the rows stay, and the list only loses its bottom edge. The strip holds up to 3 lines, in the logs, Search, Findings, Inspector Report and Hierarchy 1 line; a longer text scrolls inside it and the buttons do not move. Some strips have a field in the button row, for example a file name.

Empty table. A table with no rows says so in its first row. When rows exist but a filter hides them, the row says how many and which filter hides them, and has a button in the same row that resets that filter, for example in Console Output, Runtime Log, Build Log, Search Results and Assets.

One area. Each pane has one content area. A pane with several parts, Session and Release Candidate, shows them as titled sections in that one list, and a section title can carry its own field and buttons.

A bigger question covers the whole tile, under its header and tabs: Unsaved changes, Save As and what to do with a swap file in a Code tile, Browse in Settings, and the deletion of a folder in Files. It has a title, the text, a line for a message and its buttons. When you answer, the tile comes back as it was.

The status bar at the bottom has four parts. A count is shown up to 999, and then as 999+. Click Tools, the missing-tools count, to open Settings.

Code layout. Files on the left, a Code tile in the middle and the Game with Runtime Controls on the right. Console Output, Inspector Report, Terminal and Search Results are tabs below. Files is 176 px wide in a 1280x720 window and 240 px in 1920x1080. To search the whole project, press Ctrl+Shift+F.

Code tile. It is a text editor, Neovim inside. To open a file, double click it in Files. Each file is a tab. The Vim button switches Vim mode on and off. A file with unsaved changes shows [+]. Ctrl+S saves it. Ctrl+Z and Ctrl+Shift+Z undo and redo. A question of the tile, Unsaved changes, Save As or what to do with a swap file, covers the whole tile until you answer it.

Files. It shows the project directory as it is on disk. Right click a file or folder, or use the buttons, for New File, New Folder, Rename and Delete. Open as Text opens a file in a Code tile. The folders .mppcburn, .git and .3dmppc-editor are hidden.

Game. To play, click the picture and the game gets the keyboard. To give it back, press Shift+Escape. To change the size, use Fit, Integer, 1x, 2x or 3x. The status of the Game lies over the picture, up to 3 lines. The editor sizes the Game tile so that the picture has exactly the proportions of the project's screen, 4:3 for 320x240: 400x300 in a 1280x720 window and 640x480 in 1920x1080 in the Code, Debug and Burn layouts, and 320x240 and 480x360 in Scene. It does this when a layout is applied and when the window changes size, and stops once you move a splitter yourself.

Terminal. A shell in the project directory. To stop a running command, press Ctrl+C. To paste several lines, press Ctrl+Shift+V and confirm.

Scene layout. Two columns. On the left the Scene view, and under it side by side, equal in width, Assets (with Files and Inspector Report as tabs) and Inspector. Assets has the tabs Assets, Files and Inspector Report; there is no Console Output in this layout. On the right, stacked from the top, are the Transform Toolchest, Runtime Controls, the Game and Hierarchy down to the bottom. To make a scene, choose Scene > New Scene or press Create Scene in the Scene pane. The file goes to scenes/ in the project. To open one, choose Scene > Open Scene. A scene is a list of objects. Each object has an id, a parent and an entity, and its data sits in components, the [[component]] blocks of the scene file, such as name, transform and model.

- Hierarchy lists the objects as a tree. Its one shelf holds the buttons, the search by name and the sort by File order, Name or Type; drag an object to move it under another. Right click for Duplicate, Delete and Move to Root.
- Assets lists the files of the project in one table with the columns Name, Type, Size and Folder. Select a row to see the asset. A filter that hides every file is named in the table's empty row, with a button to clear it.
- Inspector shows the chosen object: Kind, Id, Name, Entity, Visible and its Components. To change a field, edit it; Reset puts it back. To add data, press Add component. Remove takes a component off.
- The Scene view draws the objects. Shade: Wireframe, Filled or Textured. View: Persp, Top, Front or Right. Snap and Grid help to place things.
- To work on an object, press Q to select, W to move, E to rotate or R to scale, and drag in the view. Press F to fill the view with the chosen object, and Home for the view the scene opened with. Escape takes back a drag.
- Add in the Scene menu puts a Model, a Camera, a Volume or an Empty entity in the scene. To save, press Ctrl+S in the Scene pane or choose Scene > Save Scene.

A game shows a scene only if its code reads the scene file. After a change, save and choose Run > Build and Restart.

Runtime Controls. A strip of transport buttons, the place of Cancel Build and under them a status line of two lines; the strip has a fixed height and no scrollbar. The buttons get their labels when the strip is at least 640 px wide. The status line is always there and says the state; after a failed build it says Build #N failed, and while a build runs, its step; for example "Running, session 1 | Can't reload this disc: Restart (loses state)". Without a project the strip keeps the same buttons and the line says that no project is open. The strip has no note field, no Mark, Capture or Report, no frame number and no Profile: the build and the session are in the status bar, the frame is over the Game and the profile is in Run Configuration. The note field with Mark Moment, Capture Frame and Report Issue is in the Notes of Session.

Debug layout. For playing and watching. On the left, Runtime Controls lie above the Game; to their right are Session, Observe and Test Case as tabs. Below, across the whole width, are Runtime Log, Findings, Inspector Report and Toolchest as tabs. Session is one list. Above are the facts of the run, and below them is the section Notes. Its title row has the note field and the buttons Mark Moment, Capture Frame and Report Issue. Notes lists in time order, each line with its HH:MM:SS: the start of the session (number, build and profile), your marks with the frame and the text, the end of the session with the exit code, and the lines the game wrote to stderr. Open Full Log is on the line of the end of the session. Without a session there are no facts, and Notes has one line, "No session yet: Run starts one". Restart is in the Toolchest tab, beside Runtime Log. A failed build opens Inspector Report there. Observe, titled Inspector: state, reads the game's Lua values and lets you Pin them. Test Case lists the test cases of the project, with New Test Case beside them; a labelled field takes a note, and a line under it says where a result appears. In Findings, fill in Title, Steps, Expected and Actual and press Record Finding.

Burn layout. For checking a disc before it ships. Release Controls lie above the Game, on the right at the top, with large buttons: Build, Run, Player, Stop and Report. Under the buttons a status line of two lines says "Candidate #N | Ready or Not ready | K/M checks" and names the checks that have not passed; it is there in every state; the tooltip of the number shows the sha256 of the image. To make an image, press Build. To play exactly that image, press Run; to try it in the player, press Player. Release Candidate and Export are tabs on the left. Release Candidate is one list with the sections Candidates, the facts of the chosen candidate and Checks; choosing a candidate changes the facts and the checks. Build Log, Runtime Log and Inspector Report are tabs in a strip across the whole width at the bottom. The status of the Game lies over the top of the picture on a translucent backing, so the picture keeps its size. Mark each check as you test: Build, Disc loads, Player, Launch, Input, Audio, Main scenario and Exit. When all pass, Approve or Reject the candidate.

Export. The tile has four framed blocks.

- Kind. A dropdown with three kinds, and the description of the chosen one below it: Disc, one .mppcdisc file that a player runs; Bundle, a directory with the player and the disc, ready to copy and run; Web, a directory with a page that runs the game in a browser.
- Web Tools. Always shown, in the same place for every kind. For Disc and Bundle it is dimmed and says that the kind needs no web tools. For Web it says Installed. or says that Emscripten is not installed, needed once per user (about 400 MB), and offers Install Web Support. The step strip shows the install.
- Output. The destination directory. Each kind has its own default under the project, export/mppcdisc, export/bundle and export/web, and the tile remembers the path you typed for each kind until the project closes. An export replaces only an empty directory or one an earlier export made, which holds the mark file .mppcexport. It never replaces another directory, and it refuses the project itself or a directory that contains it; choose another path then. Export starts it and Cancel stops it.
- Progress. It keeps its place whatever the state. While an export runs, the step strip shows its steps. At the end the result is DONE with the path, and the button Open Folder for Disc and Bundle or Open in Browser for Web; or it is FAILED with the step and the reason, and the button Show in Inspector Report. Before the first export it says Nothing exported yet. Export keeps no list of past exports.

## Settings and tools

To see and change the tools, choose File > Settings.... The page has a row for each tool: Runtime, Burner, Inspector, Bakers and Player. Each shows the path in use and its status. To use another file, type or Browse... the Override path, then press Check and Apply. Browse opens the file browser in place of the form; pick there, or cancel, and the form comes back. Revert drops the changes.

The editor looks for the console and burner next to itself. The Bakers row is a directory: it holds mppctexbaker, mppcmusbaker, mppcmodbaker and mppcinspector, and the editor looks for them in its own directory unless you name another. The player has no default place: name it if you want to run a candidate in it. Settings are kept in settings.toml, in its [tools] section.

A missing tool turns off the commands that need it, and the status bar says Tools: N missing. A missing mppcinspector counts as a missing baker: Build, Run when it builds, Build and Restart and the exports are unavailable, and the tooltip of the button says why. The check of Inspector Report in the background needs it too. The burner runs mppcinspector from the bakers' directory, so keep it there next to the bakers.

The same page sets Vsync and the Frame rate limit. To change the size of the text, choose View > Code Text Size, Game Scale or UI Scale. To see the tools of the open project, choose Project > Project Settings; Look for Tools Again searches again.

## Where things go

The editor uses the standard directories. XDG_CONFIG_HOME, XDG_CACHE_HOME and XDG_STATE_HOME change them; otherwise ~/.config, ~/.cache and ~/.local/state are used. A folder named by a hash of the project path is shown as <hash>.

- Layouts in ~/.config/3dmppc-editor/: layout-code, layout-scene, layout-debug, layout-burn.
- Settings and tool paths in ~/.config/3dmppc-editor/settings.toml.
- Code text size, Game scale, UI scale and last layout in ~/.config/3dmppc-editor/view.
- Recent projects in ~/.config/3dmppc-editor/recent.
- Builds in ~/.cache/3dmppc-editor/<hash>/builds/<n>, with the log <n>.log.
- Burner maps beside them: <n>.map and <n>.mppcdisc.map.
- The inspector report, read for Inspector Report, in ~/.cache/3dmppc-editor/<hash>/inspector.tsv.
- Textures staged for Reload in ~/.cache/3dmppc-editor/<hash>/staging/<build number>/.
- Release candidates in ~/.cache/3dmppc-editor/<hash>/candidates/.
- Session logs in ~/.local/state/3dmppc-editor/<hash>/sessions/, the 20 newest kept.
- Memory card in ~/.local/state/3dmppc-editor/<hash>/memcard.mppccard.
- Findings, captured frames and test results in ~/.local/state/3dmppc-editor/<hash>/findings/.
- Run profiles in the project: .3dmppc-editor/project.toml.
- Scenes in the project: scenes/.
- Burner output in the project: .mppcburn/.

## Keyboard shortcuts

- Ctrl+B — build
- F5 — run, or resume a paused game
- F6 — pause
- F7 — run one frame of a paused game
- Shift+F5 — stop
- F8 — reload the entry script, a module or a texture in the running game
- Ctrl+S — save the file in a Code tile, or the scene in a Scene pane
- Ctrl+Shift+S — Save All, from any pane
- Ctrl+O — Open Project; it does not work while a Code tile or the Terminal has the keyboard
- Ctrl+N — open the New Project page; it does not work while a Code tile or the Terminal has the keyboard
- Ctrl+Z — undo
- Ctrl+Shift+Z — redo
- Ctrl+Y — redo a scene edit
- Ctrl+Shift+F — search the whole project
- Ctrl+F6 — move the keyboard to the next tile
- Ctrl+Shift+F6 — move the keyboard to the previous tile
- Shift+Escape — give the keyboard back from the Game
- Escape — cancel a drag of a tile or of an object, or close the New Scene form
- Ctrl+D — duplicate the chosen scene object
- Delete — delete the chosen scene object
- Ctrl+C — copy the selected lines of a log, or stop the running command in a Terminal
- Ctrl+Shift+V — paste into a Terminal
- Vim and the shell keep Ctrl+O and Ctrl+N; press Ctrl+F6 to move the keyboard out of the tile first.
- F1 — open this manual; Help > Keyboard Shortcuts lists the keys in the editor
