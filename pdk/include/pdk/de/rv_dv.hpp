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
	// ЗАРЕЗЕРВИРОВАНО. Сейчас писатель кладёт сюда RV_MPPC_NOTE_MAGIC_DEF
	// побайтово, а читатель (rv_pcloader::pre_dlopen_check) поле не сверяет —
	// то есть 8 байт из 16 сейчас ничего не проверяют.
	//
	// Задумано под контрольную сумму собранного disc.so, чтобы консоль ловила
	// подмену кода, а не только несовпадение версии. До того как формат
	// застынет, надо решить два вопроса:
	//
	//   1. Сумму от файла нельзя хранить в самом файле: запись поля меняет
	//      байты, по которым сумма считалась. Выходы — считать не по всему
	//      файлу (исключив эту заметку), обнулять поле перед подсчётом и так
	//      же обнулять при проверке, либо хранить сумму вне disc.so, рядом в
	//      архиве. Обе стороны обязаны делать это одинаково.
	//   2. Восемь байт — это короткая контрольная сумма, не SHA-256 (32).
	//      Расширение поля тянет за собой static_assert ниже и делает
	//      несовместимыми все уже прожжённые диски.
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
