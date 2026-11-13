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

Every build goes to a new numbered directory. Run and Build and Restart start the game only if the burner ends with exit code 0. A running game keeps running while the next build is made.

## When a build is refused

Before it builds, mppcburner runs mppcinspector on the disc. Without mppcinspector in the bakers' directory the editor does not start a build at all. If the inspector finds an error in the sources, the build is refused and nothing is compiled.

To see what is wrong, open Problems. Every finding has the source inspector, a file, a line and a message that ends with the rule and the key in brackets. Problems also shows warnings, and it fills up again when you open the project, when you save a file and after every build.

To jump to the place, double click the row or press Enter on it. The file opens at that line.

The burner prints each finding as one line. For example, if disc.toml has files = ["assets/snd/*.wave"] under [assets] and no file ends in .wave, the build stops with this line.

disc.toml:11: error: pattern 'assets/snd/*.wave' matched no files under '.' [disc-toml-patterns, key [assets] files]

Read it from left to right. disc.toml:11 is the file and the line. error means the build is refused; a warning would not stop it. The text says what is wrong: the pattern matches nothing. The brackets name the rule, disc-toml-patterns, and the key, [assets] files.

To fix it, edit that line of disc.toml so the pattern matches real files, for example assets/snd/*.wav, or add the files it should find. Press Ctrl+S, then Ctrl+B. The row leaves Problems when the inspector runs again and finds nothing.

Compiler errors and warnings from the build are in Problems too, with the source build. The whole output is in Build Log.

## Layouts and panes

There are four layouts: Code, Scene, Debug and Burn. To switch, click one at the right end of the menu bar or choose Window > Reference Layouts. Each keeps its own tiles. Window > Reset Layout puts the chosen one back to its standard tiles. Switching changes only what is shown, never what is built, running or edited.

A tile holds one pane or several as tabs. To move a pane, drag its tile header or tab to the edge of another tile; Escape cancels. To close a tile use the X in its header, and to maximize it use the M. Right click the header to split, change or close it. To add a pane, choose Window > New Tile. To move the keyboard to the next tile, press Ctrl+F6.

Code layout. Files on the left, a Code tile in the middle and the Game with Runtime Controls on the right. Console Output, Problems, Terminal and Search Results are tabs below. To search the whole project, press Ctrl+Shift+F.

Code tile. It is a text editor, Neovim inside. To open a file, double click it in Files. Each file is a tab. The Vim button switches Vim mode on and off. A file with unsaved changes shows [+]. Ctrl+S saves it. Ctrl+Z and Ctrl+Shift+Z undo and redo.

Files. It shows the project directory as it is on disk. Right click a file or folder, or use the buttons, for New File, New Folder, Rename and Delete. Open as Text opens a file in a Code tile. The folders .mppcburn, .git and .3dmppc-editor are hidden.

Game. To play, click the picture and the game gets the keyboard. To give it back, press Shift+Escape. To change the size, use Fit, Integer, 1x, 2x or 3x.

Terminal. A shell in the project directory. To stop a running command, press Ctrl+C. To paste several lines, press Ctrl+Shift+V and confirm.

Scene layout. To make a scene, choose Scene > New Scene or press Create Scene in the Scene pane. The file goes to scenes/ in the project. To open one, choose Scene > Open Scene. A scene is a list of objects. Each object has an id, a parent and an entity, and its data sits in components, the [[component]] blocks of the scene file, such as name, transform and model.

- Hierarchy lists the objects as a tree. Search by name, sort by File order, Name or Type, and drag an object to move it under another. Right click for Duplicate, Delete and Move to Root.
- Inspector shows the chosen object: Kind, Id, Name, Entity, Visible and its Components. To change a field, edit it; Reset puts it back. To add data, press Add component. Remove takes a component off.
- The Scene view draws the objects. Shade: Wireframe, Filled or Textured. View: Persp, Top, Front or Right. Snap and Grid help to place things.
- To work on an object, press Q to select, W to move, E to rotate or R to scale, and drag in the view. Press F to fill the view with the chosen object, and Home for the view the scene opened with. Escape takes back a drag.
- Add in the Scene menu puts a Model, a Camera, a Volume or an Empty entity in the scene. To save, press Ctrl+S in the Scene pane or choose Scene > Save Scene.

A game shows a scene only if its code reads the scene file. After a change, save and choose Run > Build and Restart.

Debug layout. For playing and watching. Runtime Controls pause and step the game. Session says what the run is and holds the marks you set with the Session Toolchest: Mark, Capture, Report and Restart. Inspector: state reads the game's Lua values and lets you Pin them. Test Case lists the test cases of the project. In Findings, fill in Title, Steps, Expected and Actual and press Record Finding.

Burn layout. For checking a disc before it ships. To make an image, press Build Candidate. To play exactly that image, press Run Candidate; to try it in the player, press Player. Mark each check as you test: Build, Disc loads, Player, Launch, Input, Audio, Main scenario and Exit. When all pass, Approve or Reject the candidate. Export writes the disc, the player or a web page into a directory.

## Settings and tools

To see and change the tools, choose File > Settings.... The page has a row for each tool: Runtime, Burner, Inspector, Bakers and Player. Each shows the path in use and its status. To use another file, type or Browse... the Override path, then press Check and Apply. Revert drops the changes.

The editor looks for the console and burner next to itself. The Bakers row is a directory: it holds mppctexbaker, mppcmusbaker, mppcmodbaker and mppcinspector, and the editor looks for them in its own directory unless you name another. The player has no default place: name it if you want to run a candidate in it. Settings are kept in settings.toml, in its [tools] section.

A missing tool turns off the commands that need it, and the status bar says Tools: N missing. A missing mppcinspector counts as a missing baker: Build, Run when it builds, Build and Restart and the exports are unavailable, and the tooltip of the button says why. The check of Problems in the background needs it too. The burner runs mppcinspector from the bakers' directory, so keep it there next to the bakers.

The same page sets Vsync and the Frame rate limit. To change the size of the text, choose View > Code Text Size, Game Scale or UI Scale. To see the tools of the open project, choose Project > Project Settings; Look for Tools Again searches again.

## Where things go

The editor uses the standard directories. XDG_CONFIG_HOME, XDG_CACHE_HOME and XDG_STATE_HOME change them; otherwise ~/.config, ~/.cache and ~/.local/state are used. A folder named by a hash of the project path is shown as <hash>.

- Layouts in ~/.config/3dmppc-editor/: layout-code, layout-scene, layout-debug, layout-burn.
- Settings and tool paths in ~/.config/3dmppc-editor/settings.toml.
- Code text size, Game scale, UI scale and last layout in ~/.config/3dmppc-editor/view.
- Recent projects in ~/.config/3dmppc-editor/recent.
- Builds in ~/.cache/3dmppc-editor/<hash>/builds/<n>, with the log <n>.log.
- Burner maps beside them: <n>.map and <n>.mppcdisc.map.
- The inspector report, read for Problems, in ~/.cache/3dmppc-editor/<hash>/inspector.tsv.
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
