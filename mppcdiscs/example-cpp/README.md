# example-cpp — Example mppcdisc

`src/example-cpp.cpp` is a hand-written `rv_de`, not generated code: its
`rv_dmain` derives from `rv_dmain_base_`, a class
`RV_MPPC_DISC_CPP_DEF` (`pdklib/rv_cppdisc/rv_cppdisc.hpp`) defines with the
startup guards, MENU-button press-edge tracking and `read_asset()` every
C++ disc repeats. `rv_dmain` adds only what makes this disc THIS game — the
sprite grid, the text bar and their layout — and `RV_MPPC_DISC_DEF`
(`pdk/de/rv_dv.h`), the mandatory pdk contract, plants the result. A disc is
free to skip `RV_MPPC_DISC_CPP_DEF` and write all five `rv_de` hooks by hand
instead, the way this one did before that header existed.
