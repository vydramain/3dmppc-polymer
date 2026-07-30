#pragma once

#include <cstdint>

namespace rv_pdk {

#define RV_MPPC_VERSION_INFO_SECTION_DEF ".mppc_pdk_ver"
#define RV_MPPC_NHDR_NAME_DEF "RV_MPPC_VER"

inline constexpr int RV_MPPC_VERSION_MAJOR = 0;
inline constexpr int RV_MPPC_VERSION_MINOR = 0;

// отвечает за то, а какое значение смотрим
inline constexpr const char RV_MPPC_VERSION_INFO_SECTION[] = RV_MPPC_VERSION_INFO_SECTION_DEF;
inline constexpr const char RV_MPPC_NHDR_NAME[] = RV_MPPC_NHDR_NAME_DEF;

// отвечает за обозначение секции внутри библиотеки для чтения, без исполнения кода
struct rv_mppc_version_info {
    char magic[8];  // marker {'R', 'V', '_', 'M', 'P', 'P', 'C', '\0'}
    uint32_t version_major;
    uint32_t version_minor;
};

// гарантирует побайтовую иднетичность для любой реализации
static_assert(sizeof(rv_mppc_version_info) == 16);

inline constexpr const char* RV_MPPC_VERSION_SYMBOL_CREATE = "rv_mppc_disc_create";
inline constexpr const char* RV_MPPC_VERSION_SYMBOL_DESTROY = "rv_mppc_disc_destroy";

// Консоль ищет через dlsym символы с именами из констант выше; экспортировать
// их должен сам диск. Раньше это делал макрос RV_DISC_EXPORT из удалённого
// rv_abi.hpp — нужен наследник ЗДЕСЬ, рядом с константами имён: строка
// "rv_mppc_disc_create" и имя функции в макросе — одно имя в двух написаниях,
// разъедутся — dlsym молча вернёт nullptr. Наследник обязан сажать два
// extern "C" символа с default visibility и НОВОЙ сигнатурой:
//   rv_de* rv_mppc_disc_create(int32_t version_major, int32_t version_minor);
//   void   rv_mppc_disc_destroy(rv_de* disc);
// create получает версию консоли и решает, согласен ли диск работать
// (несогласие = вернуть nullptr). Дальше по порядку:
//   1) наследник RV_DISC_EXPORT здесь;
//   2) миграция mppcdiscs/hello (см. TODO у RV_DISC_EXPORT в hello.cpp);
//   3) пересборка диска mppcburner'ом, повторный запуск — dlsym обязан пройти.

class rv_de;

using rv_mppc_disc_create_fn = rv_de* (*)(int32_t version_major, int32_t version_minor);
using rv_mppc_disc_destroy_fn = void (*)(rv_de* disc);

}  // namespace rv_pdk
