#pragma once

#include <cstdint>

namespace rv_pdk {

// Владелец ноты — поле имени внутри записи (n_name; readelf -n показывает его
// в колонке Owner). Писатель кладёт его в запись, читатель сверяет memcmp'ом.
#define RV_MPPC_NOTE_OWNER_DEF "RV_MPPC_VER"

// Штамп в начале payload'а — внутренняя перестраховка поверх owner+type.
#define RV_MPPC_NOTE_MAGIC_DEF "RV_MPPC"

inline constexpr int RV_MPPC_VER_MAJOR = 0;
inline constexpr int RV_MPPC_VER_MINOR = 0;

inline constexpr unsigned int RV_MPPC_NOTE_TYPE = 1;
inline constexpr const char RV_MPPC_NOTE_OWNER[] = RV_MPPC_NOTE_OWNER_DEF;

// Payload (descriptor) ноты — единственный кусок записи, который обе стороны
// трогают как C++-тип: писатель (pdklib/rv_disc_version.hpp) вкладывает его в
// свою объемлющую структуру, читатель (rv_pcloader) вычитывает его по n_descsz.
// Заголовок и имя записи общего типа не имеют: читатель разбирает их по байтам,
// объемлющая структура — деталь писателя.
struct rv_mppc_note_desc {
    char magic[8];  // RV_MPPC_NOTE_MAGIC_DEF, побайтово
    uint32_t version_major;
    uint32_t version_minor;
};

// гарантирует побайтовую идентичность для любой реализации
static_assert(sizeof(rv_mppc_note_desc) == 16);

// ---

#define RV_MPPC_STR_DEF_(x) #x
#define RV_MPPC_STR_DEF(x) RV_MPPC_STR_DEF_(x)

#define RV_MPPC_DISC_ENTRY_CREATE_DEF rv_mppc_disc_entry_create_fn
#define RV_MPPC_DISC_ENTRY_DESTROY_DEF rv_mppc_disc_entry_destroy_fn

inline constexpr const char* RV_MPPC_DISC_ENTRY_CREATE =
    RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_CREATE_DEF);
inline constexpr const char* RV_MPPC_DISC_ENTRY_DESTROY =
    RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_DESTROY_DEF);

// Консоль ищет через dlsym символы с именами из констант выше; экспортирует их
// макрос RV_MPPC_DISC_ENTRY_DEF ниже. Имя функции и строка для dlsym выводятся
// из одного токена *_DEF, поэтому разъехаться не могут.
//
// В версии диск НЕ участвует: create ничего не принимает и ничего не решает.
// Версию совместимости консоль читает из ELF-ноты (пишет её
// pdklib/rv_disc_version.hpp, читает rv_pcloader::pre_dlopen_check) ещё до
// dlopen — код диска к этому моменту не исполнялся.

class rv_de;

using rv_mppc_disc_create_fn = rv_de* (*)();
using rv_mppc_disc_destroy_fn = void (*)(rv_de* disc);

#define RV_MPPC_DISC_ENTRY_DEF(disc_class)                                                 \
    extern "C" __attribute__((visibility("default"))) rv_pdk::rv_de*                       \
    RV_MPPC_DISC_ENTRY_CREATE_DEF() {                                                      \
        return new disc_class();                                                           \
    }                                                                                      \
    extern "C" __attribute__((visibility("default"))) void RV_MPPC_DISC_ENTRY_DESTROY_DEF( \
        rv_pdk::rv_de* disc) {                                                             \
        delete disc;                                                                       \
    }

}  // namespace rv_pdk
