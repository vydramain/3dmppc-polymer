# UX технических приложений SGI IRIX

Что делало интерфейс рабочих станций SGI узнаваемым: правила самой SGI для приложений IRIX и
устройство 3D-приложений того времени. Документ - исследование, а не требование: требования живут в
`3dmppc-editor-v0.4-requirements.md`, решения - в `adr/`.

Первоисточник - «Indigo Magic User Interface Guidelines» (Silicon Graphics, 007-2167-001), главы 3
и 9. Где вывод сделан по снимкам экрана, а не по тексту, это сказано явно.

## Вид Indigo Magic поверх IRIS IM (Motif)

IRIS IM - порт OSF/Motif от SGI. Вид Indigo Magic отличается от стандартного Motif так:

- Мягкое скруглённое затенение вместо острых фасок: «Numerous sharp bevels, such as those found in
  standard Motif components, detract from rather than add to the visual presentation». Общая
  эстетика - «burnished aluminum», матовый алюминий.
- Чёрная обводка вокруг отдельных виджетов - кнопок, скроллбаров, - чтобы они отделялись от фона
  окна.
- Плоские «деколи» вместо выпуклых 3D-элементов: стрелка нарисована на кнопке, а не выдавлена; «no
  gratuitous use of 3D».
- Составные элементы собраны в одно целое: кнопки-стрелки встроены в скроллбар, скроллбар
  визуально слит с панелью, которую прокручивает.
- У бегунка скроллбара рифлёная ручка (grip); на время прокрутки на исходном месте остаётся
  вдавленный след.
- Отметка выбора явная: красная галочка у чекбокса, синий треугольник у радиокнопки.
- Locate highlight: живой элемент светлеет под курсором, пассивная графика и отключённые элементы -
  нет. По этому пользователь видит, что можно нажать и что приложение слушает.
- Мнемоники в меню - штриховое подчёркивание (stroked underline).

## Цвет и шрифт

- Схема по умолчанию - нейтральная серая палитра и Helvetica. Нейтральный хром «preserves the use of
  color for the application's content areas»: цвет достаётся содержимому, а не интерфейсу.
- Схема - набор цветов и шрифтов под абстрактными именами; приложение ссылается на имена, пользователь
  выбирает схему. На содержимое вроде окна рендера схема не распространяется.
- Цвет по роли области: cadet blue - поля для чтения и записи (drop pockets, полки), Navajo white -
  поля только для чтения.

## Окна

- Четыре вида окон: главное (main primary, одно на приложение), со-главное (co-primary, основная
  работа вне главного), вспомогательное (support - постоянная панель или палитра инструментов),
  диалог (короткий ввод или сообщение).
- Декорации 4Dwm: слева кнопка оконного меню, затем заголовок, справа minimize и maximize, по краю -
  рамка изменения размера.

## Контролы SGI

- Thumbwheel - колесо: непрерывное значение крутится протягиванием. Конечный диапазон - для зума,
  бесконечный - для вращения 3D-объекта. Может иметь кнопку «home», которая возвращает значение по
  умолчанию. Для дискретных значений - шкала или диск, не колесо.
- Drop pocket - гнездо, в которое перетаскивают иконку файла.
- Стандартный Color Chooser - вспомогательное окно.

## 3D-приложения

- Open Inventor examiner viewer - эталонная рамка 3D-вида: колесо RotX слева, RotY снизу, Dolly
  справа; справа столбец кнопок - выбор, вращение вида, справка, home, set home, view all, seek,
  перспектива или ортография. Мышь: левая вращает вокруг фокуса, средняя двигает, левая и средняя
  вместе - приближение. Наезд на точку - seek.
- Alias PowerAnimator и первые Maya - marking menus и hotbox: радиальные меню у курсора по
  модификатору или пробелу.
- Softimage 3D - наборы меню по этапу работы: Model, Motion, Actor, Matter, Tools.
- По снимкам экрана (вывод, не цитата): интерфейс плотный и текстовый, несколько 3D-видов
  одновременно, инструменты вынесены в палитры вспомогательных окон.

## Сопоставление с редактором

Совпадает с правилами SGI и с референсами из `references/`:

- чёрная обводка отдельных виджетов;
- locate highlight - заметное изменение элемента под курсором;
- деколи вместо выпуклых стрелок;
- колёса и столбец кнопок вьюера (home, view all, seek) для тайлов Game и Scene;
- главное окно плюс вспомогательные панели.

Расходится: UI-02 требует ступенчатых bevel-рамок и палитры раннего Steam/VGUI2, а Indigo Magic -
мягкого затенения и нейтральной серой палитры. Выбор между ними - решение владельца.

## Источники

- [Indigo Magic User Interface Guidelines, 007-2167-001](https://irix7.com/techpubs/007-2167-001.pdf)
- [IRIX Interactive Desktop](https://en.wikipedia.org/wiki/IRIX_Interactive_Desktop)
- [MaXX Interactive Desktop: Schemes and Themes](https://docs.maxxinteractive.com/books/customization/page/schemes-and-themes)
- [SceneViewer, демонстрация Open Inventor](https://web.mit.edu/6.837/inventor/usr/demos/Inventor/SceneViewer.about)
- [SoQtExaminerViewer, Coin3D](https://www.coin3d.org/SoQt/html/classSoQtExaminerViewer.html)
- [SoQtExaminerViewer(3)](https://manpages.org/soqtexaminerviewer/3)
- [Power Animator 9 in IRIX](https://forums.irixnet.org/thread-1640.html)
- [Softimage 3D](https://en.wikipedia.org/wiki/Softimage_3D)
- [Autodesk Maya](https://en.wikipedia.org/wiki/Autodesk_Maya)
- [The Hotbox: Efficient Access to a Large Number of Menu-Items](https://www.researchgate.net/publication/221517830_The_Hotbox_Efficient_Access_to_a_Large_Number_of_Menu-Items)
