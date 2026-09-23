# 0001. UI toolkit: Dear ImGui и SDL_Renderer

Статус: принято. Закрывает ARC-08 в части toolkit и backend.

## Контекст

Редактору нужен UI toolkit с тайлингом, собственными IRIX-контролами, текстовым вводом, кириллицей,
HiDPI и clipboard. Кандидат из требований - Dear ImGui.

У Dear ImGui нет собственной системы сборки, и это намеренно: библиотека - это несколько `.cpp`,
которые компилирует проект-потребитель. Ядро: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp`. Платформа и рендерер подключаются файлами из `backends/`.

## Решение

- Dear ImGui, тег `v1.92.9b`, ветка `master`. Ветка `docking` не нужна: тайлинг свой (0002).
- Платформа: `backends/imgui_impl_sdl3.cpp`. SDL3 подключается через общий `cmake/sdl.cmake`.
- Рендерер: `backends/imgui_impl_sdlrenderer3.cpp`. Код рисования не зависит от рендерера напрямую,
  чтобы позже можно было перейти на Vulkan заменой backend.
- ImGui копируется в `third_party/imgui/` по правилам `third_party/README.md`: только нужные файлы
  ядра и двух backend, `LICENSE.txt`, `ORIGIN.md` с тегом и SHA-256 каждого файла. Не submodule и не
  `FetchContent`.
- `editor/CMakeLists.txt` собирает из этих файлов статическую библиотеку `3dmppc_editor_imgui`,
  include-каталоги ImGui подключаются как `SYSTEM`.
- Настройки ImGui задаются своим заголовком через `IMGUI_USER_CONFIG` из `editor/src/`, а не правкой
  `imconfig.h` в `third_party/`.
- `imgui_internal.h` включает один файл редактора - загрузчик шрифта (`ImFontLoader` и упаковка
  атласа). Виджеты обходятся публичным API. Внутренний API ImGui не стабилен, и обновление тега
  должно задевать одно место.
- Сглаживания нет. Backend по умолчанию ставит текстурам `SDL_SCALEMODE_LINEAR`; редактор раз в кадр
  ставит в background draw list, который рисуется первым, стандартный callback
  `GetPlatformIO().DrawCallback_SetSamplerNearest`
  (есть в `imgui_impl_sdlrenderer3.cpp` с 2026-04-23).

## Последствия

- Консоль и авторские инструменты ImGui не видят. Player-сборка его не собирает: редактор попадает
  только в сборку с `-D3DMPPC_DEVTOOLS=ON`.
- Многострочный ввод ImGui не используется как редактор кода; код редактируется в nvim (0005).
