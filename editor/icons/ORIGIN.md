# The editor's icons

Drawn for this project by `draw_icons.py` (spec section 16): every picture is a few shapes on a
32-unit grid, rasterized without smoothing at 16, 24, 32 and 48 px, then given the shared dark
outline (#20251c) and a light top-left edge. The colours are the theme's and Catppuccin Mocha's
accents. Nothing is copied from another icon set.

To change an icon, edit its `i_<name>` function and run `python3 draw_icons.py .` here (Pillow);
commit the script and the PNGs together. `<name>-<size>.png` is what the editor loads.
