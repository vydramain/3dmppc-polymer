#pragma once

#include "pdk/de/rv_de.hpp"
#include "pdk/rv_pdko.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pchost.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// PATTERN: composition root. This is the single place where the concrete
// machine is assembled — the host, the five controllers, and the geometry they
// were built from. Nothing below constructs a subsystem: a controller receives
// what it needs and never reaches sideways for it.
class rv_pconsole : public rv_pdk::rv_pdko
{
private:
    rv_pconsole_params params_;

    // BORROWED, not owned: the host is created and prepared by the CALLER
    // before the console can even be built (checking the disc's budget needs
    // to know which SDL subsystems came up), so the console can no longer own
    // it. cio_ and cv_ still borrow it in turn, which only works because the
    // caller is guaranteed to outlive this console.
    rv_pchost &host_;

    // TODO(Claude-инструкция, код твой). Объяви здесь поле rv_pccl cl_;
    // и включи "rv_pconsole/cl/rv_pccl.hpp" наверху.
    //
    // ПОРЯДОК ОБЪЯВЛЕНИЯ ВАЖЕН, как и для host_ выше. Поля разрушаются в
    // порядке, ОБРАТНОМ объявлению. Поставь cl_ в общий ряд контроллеров, по
    // алфавиту.
    rv_pcca ca_;
    rv_pccd cd_;
    rv_pccio cio_;
    rv_pccm cm_;
    rv_pccv cv_;

    // BORROWED, never owned. The loader reads the manifest BEFORE this console
    // exists — the numbers it finds are what this console is built from — so it
    // cannot live inside the thing it configures. main() owns it and must let it
    // die FIRST: its teardown runs disc_shutdown(), a hook allowed to touch every
    // controller above. Null when the built-in disc is running.
    rv_pcloader *loader_ = nullptr;

public:
    rv_pconsole(const rv_pconsole_conf &conf, rv_pchost &host, rv_pcloader *loader);

    ~rv_pconsole() = default;

    rv_pdk::rv_ca *ca() override;
    rv_pdk::rv_cd *cd() override;
    rv_pdk::rv_cio *cio() override;
    rv_pdk::rv_cm *cm() override;
    rv_pdk::rv_cv *cv() override;

    // TODO(Claude-инструкция, код твой). Добавь rv_pdk::rv_cl* cl() override;
    // Тело в rv_pconsole.cpp — одна строка, возврат адреса поля. Смотри, как
    // сделаны соседи, и повтори.
    //
    // ЗАГЛУШКА до появления поля rv_pccl cl_: контракт rv_pdko требует cl(),
    // иначе rv_pconsole абстрактен и не собирается. Возвращает nullptr —
    // ни один диск в дереве его пока не дёргает. Заменить телом «return &cl_;»
    // сразу, как поле появится.
    rv_pdk::rv_cl *cl() override;

    // The drive itself, console-side. rv_pdko::cd() hands a disc the CONTRACT's
    // view (rv_cd, which cannot load a medium); putting a medium IN the drive is
    // the machine operator's act, not the disc's, so it goes through here.
    rv_pccd &drive()
    {
        return cd_;
    }

    // PATTERN: inversion of control. The frame loop belongs to the console; the
    // disc lives inside the rv_de hooks and never owns a loop of its own.
    //
    // Returns RV_OK when the run ends normally (the disc asked to stop, the
    // frame budget ran out, or the user powered the machine off), or a negative
    // rv_err if the disc refused to initialize. A display that would not come
    // up is a warning, not a stop.
    int64_t disc_run(rv_pdk::rv_de &disc);

    // Did every resource this console was built from actually come into
    // existence? Covers audio (ca_), video (cv_) and the memory card (cm_). A
    // budget the machine accepted at stage E3 can still fail to materialise at
    // stage G — an address-space reservation is allowed to refuse, and so is
    // the card's backing image. False means the machine did not provide what
    // the disc declared, and the run must not start.
    bool ready() const;
};

} // namespace rv_3dmppc
