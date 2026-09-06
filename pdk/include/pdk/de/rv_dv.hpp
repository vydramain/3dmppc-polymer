#pragma once

#include <cstdint>

namespace rv_pdk
{

#define RV_MPPC_SECTION_NAME_DEF ".note.rv_mppc_ver"

#define RV_MPPC_NOTE_OWNER_DEF "RV_MPPC_VER"
#define RV_MPPC_NOTE_MAGIC_DEF "RV_MPPC"

inline constexpr int RV_MPPC_VER_MAJOR = 0;
inline constexpr int RV_MPPC_VER_MINOR = 0;

inline constexpr unsigned int RV_MPPC_NOTE_TYPE = 1;
inline constexpr const char RV_MPPC_NOTE_OWNER[] = RV_MPPC_NOTE_OWNER_DEF;

struct rv_mppc_note_desc {
	// Контрольная сумма собранного disc.so: усечённый до 8 байт SHA-256
	// (сознательное усечение, не полный дайджест) от секций .text, .rodata и
	// .data, каждая с префиксом её uint64 LE-размера. Общая реализация —
	// pdklib/rv_disc_hash (rv_disc_hash_compute считает сумму,
	// rv_disc_hash_magic_offset находит это поле в файле по .note.rv_mppc_ver).
	//
	// Заметка .note.rv_mppc_ver в сумму не входит, поэтому burner может
	// дописать сюда сумму уже после линковки disc.so, не инвалидируя её же:
	// байты этого поля не участвуют в расчёте ни на записи, ни на проверке.
	// Burner пишет сумму сюда после сборки, rv_pcloader::pre_dlopen_check
	// пересчитывает её и сверяет перед dlopen.
	char magic[8];
	uint32_t version_major;
	uint32_t version_minor;
};

static_assert(sizeof(rv_mppc_note_desc) == 16);

#define RV_MPPC_STR_DEF_(x) #x
#define RV_MPPC_STR_DEF(x)  RV_MPPC_STR_DEF_(x)

#define RV_MPPC_DISC_ENTRY_CREATE_DEF  rv_mppc_disc_entry_create_fn
#define RV_MPPC_DISC_ENTRY_DESTROY_DEF rv_mppc_disc_entry_destroy_fn

inline constexpr const char *RV_MPPC_DISC_ENTRY_CREATE =
	RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_CREATE_DEF);
inline constexpr const char *RV_MPPC_DISC_ENTRY_DESTROY =
	RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_DESTROY_DEF);

class rv_de;

using rv_mppc_disc_create_fn = rv_de *(*)();
using rv_mppc_disc_destroy_fn = void (*)(rv_de *disc);

#define RV_MPPC_DISC_ENTRY_DEF(disc_class)                                                 \
	extern "C" __attribute__((visibility("default"))) rv_pdk::rv_de *                      \
	RV_MPPC_DISC_ENTRY_CREATE_DEF()                                                        \
	{                                                                                      \
		return new disc_class();                                                           \
	}                                                                                      \
	extern "C" __attribute__((visibility("default"))) void RV_MPPC_DISC_ENTRY_DESTROY_DEF( \
		rv_pdk::rv_de *disc)                                                               \
	{                                                                                      \
		delete disc;                                                                       \
	}

} // namespace rv_pdk
