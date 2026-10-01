# example-cpp — Example mppcdisc

`src/example-cpp.cpp` is a hand-written `rv_de`, not generated code: its
`rv_dmain` derives from `rv_dmain_base_`, a class
`RV_MPPC_DISC_CPP_DEF` (`pdklib/rv_cppdisc/rv_cppdisc.hpp`) defines with the startup guards, MENU-button press-edge tracking, `read_asset()`, the
screen's size and the frame plumbing — `frame_begin`/`frame_end`,
`texture_resident` and `draw_sprite` — every C++ disc repeats. `rv_dmain` adds only what makes this disc THIS game — the
sprite grid, the text bar and their layout — and `RV_MPPC_DISC_ENTRY_DEF`
(`pdk/de/rv_dv.h`), the mandatory pdk contract, plants the result. A disc is
free to skip `RV_MPPC_DISC_CPP_DEF` and write all five `rv_de` hooks by hand
instead, the way this one did before that header existed.

`scenes/main.scene.toml` is the disc's scene: a camera and one box, in the
scene format `pdklib/rv_scene/rv_scene.hpp` reads. `[assets]` copies it onto
the disc; `draw_scene()` reads it with `pdklib/rv_scene` and draws each mesh
object as a box through the scene's camera. A disc gets pdklib as headers
only, so this file includes `pdklib/rv_scene/rv_scene_unit.hpp` once to
compile the loader in. Edit the scene in 3dmppc-editor's Scene layout, Save,
Build and Run: a changed scene needs a restart, there is no scene reload.
