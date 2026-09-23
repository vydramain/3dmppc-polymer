# 0003. Тема и компоненты

Статус: принято. Относится к UI-01..UI-07.

## Контекст

`ImGuiStyle` задаёт цвета, отступы и скругления. Двухгранных bevel-рамок, stippled-заголовков,
ромбовидных radio и стрелочных скроллбаров (UI-02) в нём нет.

## Решение

Три слоя, каждый зависит только от предыдущего.

1. Токены - данные, без вызовов ImGui:

   ```cpp
   struct rv_editor_theme {
       uint32_t window, inset, button, selection, bevel_hi, bevel_lo, text, code_base;
       float bevel_px, pad_px, ui_scale;
   };
   ```

   Значения - таблица токенов из раздела 11 требований. Одна функция переносит токены в
   `ImGuiStyle`, поэтому штатные виджеты ImGui сразу получают палитру (UI-01).
2. Примитивы рисования поверх `ImDrawList`: bevel raised/sunken, stipple, diamond, стрелки
   скроллбара. Ввода не знают.
3. Виджеты в форме вызовов ImGui: `bool button(label, state)` и т. п. Ввод и состояния
   hover/pressed/focused/disabled берутся у ImGui через публичный `InvisibleButton` и
   `IsItemHovered`/`IsItemActive`/`IsItemFocused`, рисуются слоем 2.
   Disabled-состояние несёт причину недоступности (UI-04).

Widget Catalog (UI-07) - обычная панель, которая рисует каждый виджет во всех состояниях.

## Иконки

Из `github.com/OmskGameJam/win-55-ui` (MIT, коммит `7fe2b31a76f9eeff31f12e2fbdc41710146c3c6f`)
берутся только иконки `public/win-55-ui/icons/*.png`. Автор нарисовал их заново по мотивам Win95.
Они копируются в `third_party/` с `LICENSE.md` и `ORIGIN.md`.

Не берутся:

- шрифты `Standard-*`: растеризованы из Liberation Sans 1.x (GPLv2 с font exception) и Noto Sans JP,
  поэтому MIT репозитория на них не распространяется;
- emoji `000`-`BA1`: работы NTT DoCoMo и KDDI, исключены из MIT самим репозиторием;
- 9-patch рамки и курсоры: рамки рисует слой 2.
