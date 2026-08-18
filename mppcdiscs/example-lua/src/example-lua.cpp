#include <cstdint>

#include "pdk/de/rv_de.hpp"
#include "pdk/de/rv_dv.hpp"

namespace example_lua
{

namespace
{

struct rv_example_lua_texheader {
	uint16_t version;
};

} // namespace

class rv_dmain : public rv_pdk::rv_de
{
public:
	int64_t disc_initialize(rv_pdk::rv_pdko &pdk) override;
	void frame_update(float dt) override;
	void frame_render() override;
	bool disc_release() const override
	{
		return release_;
	}
	void disc_shutdown() override;
	const char *disc_tilte() const
	{
		return "example-lua";
	};

private:
	rv_pdk::rv_pdko *pdk_ = nullptr;
	bool release_ = false;

	// TODO(Claude-инструкция, код твой). Одно поле: handle чанка, который вернул
	// cl->script_load(). Инициализируй значением «нет чанка».
	//
	// lua_State здесь НЕ хранится и не создаётся: VM принадлежит консоли, диск
	// её только просит через pdk_->cl(). Тот же принцип, что с видеопамятью —
	// hello.cpp держит адреса (addr_texels_), а не сам буфер.
};

// ─── ЗАДАНИЕ: тела хуков ──────────────────────────────────────────────────────
// Комментарии написаны Claude (claude-opus-5). Код пишешь ты.
//
// Сейчас у класса объявлены методы, но ни одного определения нет — этот файл
// не слинкуется. Ниже, что должно быть в каждом теле.
//
//
// TODO(1). ОПЕЧАТКА, чинить первой: метод называется disc_tilte, а в контракте
//   (pdk/de/rv_de.hpp) — disc_title. Значит это не переопределение, а новый
//   метод, и класс остаётся абстрактным: чисто виртуальный disc_title() никто
//   не реализовал. Добавь override, как у соседних методов, — компилятор тогда
//   ловит такие опечатки сам.
//
//
// TODO(2). ЕЩЁ ОДНА ОПЕЧАТКА, чинить второй: каталог со скриптами называется
//   sciprts/, а disc.toml просит "scripts/*.lua". Глоб не совпадёт, burner
//   молча возьмёт ноль скриптов, и ты будешь искать ошибку в Lua, которой там
//   нет. Переименуй каталог.
//
//
// TODO(3). disc_initialize(pdk)
//
//   Порядок:
//     pdk_ = &pdk;
//     взять контроллеры: pdk_->cd(), pdk_->cl(); проверить оба на nullptr
//     прочитать "example-lua.luac" с носителя — read_asset() из hello.cpp
//       делает ровно это (asset_open -> asset_size -> asset_read), возьми его
//       за образец; имя файла — то, что burner положил в архив
//     chunk_ = cl->script_load(bytes.data(), bytes.size(), "example-lua")
//     если chunk_ < 0 — вернуть отрицательный rv_err, НЕ RV_OK
//
//   ВОТ ЭТА СТРОЧКА со script_load и есть «диск показывает консоли, где
//   байткод». Никакого объявления в манифесте для этого не нужно: диск не
//   заявляет, что у него есть скрипты, — он просто передаёт байты.
//
//   Буфер с байткодом после script_load можно отпустить: консоль скопировала
//   всё, что ей нужно, внутрь VM. Ровно как video_asset_write копирует текселы.
//
//
// TODO(4). frame_update(dt)
//
//   cl->stack_push_number(dt), затем cl->script_call(chunk_, "frame_update", 1, 1).
//   Результат (хочет ли скрипт выключиться) прочитать в release_ и снять со
//   стека.
//
//   Если script_call вернул ошибку — залогируй ОДИН раз и запомни флагом, что
//   хук сломан, иначе будешь печатать одно и то же шестьдесят раз в секунду.
//
//
// TODO(5). frame_render()
//
//   cl->script_call(chunk_, "frame_render", 0, 0).
//
//   ВАЖНО: cv->frame_flush() всё равно зовёт C++, а не скрипт. Пока Lua не
//   умеет рисовать (обратное направление — консоль биндит железо в VM — ещё не
//   спроектировано), скрипт может только считать и печатать через print().
//   Это нормально для первого шага: сначала убедись, что байткод вообще
//   исполняется, и только потом думай про рисование из Lua.
//
//
// TODO(6). disc_shutdown()
//
//   cl->script_free(chunk_) и обнулить поле. Последний момент, когда фасад
//   ещё жив — после возврата консоль вправе выгрузить код диска целиком.
//
//
// TODO(7). Последней строкой файла — RV_MPPC_DISC_ENTRY_DEF(example_lua::rv_dmain);
//   Без неё в disc.so нет экспортируемых символов, и консоль не найдёт диск
//   через dlsym. Смотри hello.cpp и pdk/de/rv_dv.hpp.
//
//
// ПОРЯДОК РАБОТЫ. Не пиши всё сразу. Первый рубеж — увидеть в терминале
// «Hello from example lua!» из scripts/example-lua.lua. Для этого хватит
// TODO(3) и одного script_load: строка печатается ТЕЛОМ чанка, то есть уже на
// шаге lua_pcall внутри script_load, до всяких хуков.

} // namespace example_lua
