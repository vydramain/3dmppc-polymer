# 0007. Общий cmake/pdklib.cmake

Статус: принято.

## Контекст

Корневой `CMakeLists.txt` и `pdk/tools/CMakeLists.txt` объявляют цели `3dmppc_pdk` и
`3dmppc_pdklib` каждый у себя, одинаково; отличается только переменная корня (`CMAKE_SOURCE_DIR` и
`RV_REPO_ROOT`). Редактору нужны те же цели: разбор `disc.toml` из `rv_manifest`, который
скомпилирован внутри `3dmppc_pdklib`. Позже, возможно, `rv_textures` и `rv_zip` для превью ассетов и
чтения `.mppcdisc`. Шрифт интерфейса редактора - `rv_font` (0004).

## Решение

Цели выносятся в `cmake/pdklib.cmake`. Корень репозитория модуль вычисляет от своего расположения,
поэтому одинаково работает из любого проекта:

```cmake
include_guard(GLOBAL)
get_filename_component(RV_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${RV_REPO_ROOT}/pdk/include)

file(GLOB_RECURSE 3DMPPC_PDKLIB_SOURCES CONFIGURE_DEPENDS ${RV_REPO_ROOT}/pdk/lib/include/pdklib/*.cpp)
add_library(3dmppc_pdklib STATIC ${3DMPPC_PDKLIB_SOURCES})
target_include_directories(3dmppc_pdklib PUBLIC ${RV_REPO_ROOT}/pdk/lib/include)
target_link_libraries(3dmppc_pdklib PUBLIC 3dmppc_pdk)
```

Корень, `pdk/tools/` и `editor/` подключают модуль вместо своих строк, как сейчас подключают
`sdl.cmake` и `luajit.cmake`. Редактор линкует `3dmppc_pdklib` так же, как `mppcburner`.

## Проверка

Сборки `build`, `build-dev` и отдельная сборка `pdk/tools` дают те же бинарники; `3dmppc_pdklib`
компилируется из тех же исходников.
