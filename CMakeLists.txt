cmake_minimum_required(VERSION 3.21)
project(anime_client_cpp LANGUAGES CXX)

# В VS по умолчанию стартовый проект — приложение, не ALL_BUILD (его нельзя запустить F5).
set(CMAKE_VS_STARTUP_PROJECT anime_client_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # /utf-8: исходники сохранены в UTF-8 без BOM — без флага MSVC читает их
    # как CP1251 и портит кириллицу в строковых литералах ещё на компиляции.
    add_compile_options(/FS /utf-8)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Не ставь весь метапакет qt6 / `vcpkg install qt` — WebEngine, Multimedia,
# Quick3D и т.п. не используются. Достаточно qtbase + qtdeclarative + qtsvg
# (см. vcpkg.json и README).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network Sql OpenGL Svg Concurrent)

# --- libmpv ---
# Windows: готовая dev-сборка (shinchiro) в C:/dev/libmpv — vcpkg не тянет mpv.
# Linux/macOS: pkg-config (Arch: mpv / libmpv).
if(WIN32)
    set(MPV_ROOT_HINT "C:/dev/libmpv" CACHE PATH "Где искать libmpv (include/lib)")
    find_path(MPV_INCLUDE_DIR mpv/client.h HINTS "${MPV_ROOT_HINT}/extracted/include")
    find_library(MPV_LIBRARY NAMES mpv HINTS "${MPV_ROOT_HINT}/lib")
    set(MPV_DLL "${MPV_ROOT_HINT}/bin/libmpv-2.dll" CACHE FILEPATH "libmpv-2.dll для копирования рядом с exe")
    if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
        message(FATAL_ERROR "libmpv не найден. Укажи -DMPV_INCLUDE_DIR=... -DMPV_LIBRARY=...")
    endif()
    if(NOT EXISTS "${MPV_DLL}")
        message(FATAL_ERROR "libmpv-2.dll не найден: ${MPV_DLL} (нужен рядом с exe на Windows)")
    endif()
    message(STATUS "libmpv: ${MPV_LIBRARY}")
    message(STATUS "libmpv dll: ${MPV_DLL}")
    set(MPV_LINK_LIBS ${MPV_LIBRARY})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    set(MPV_INCLUDE_DIR ${MPV_INCLUDE_DIRS})
    set(MPV_LINK_LIBS PkgConfig::MPV)
endif()

# WIN32 subsystem only on Windows (no console window).
set(_APP_WIN32_FLAG "")
if(WIN32)
    set(_APP_WIN32_FLAG WIN32)
endif()

qt_add_executable(anime_client_cpp ${_APP_WIN32_FLAG}
    src/main.cpp
    src/core/AppConfig.h
    src/core/AppConfig.cpp
    src/core/NetworkManager.h
    src/core/NetworkManager.cpp
    src/core/EpisodeParser.h
    src/core/EpisodeParser.cpp
    src/core/HistoryManager.h
    src/core/HistoryManager.cpp
    src/core/HealthCheck.h
    src/core/HealthCheck.cpp
    src/core/TorrentStreamManager.h
    src/core/TorrentStreamManager.cpp
    src/core/StreamReadiness.h
    src/core/StreamReadiness.cpp
    src/core/MpvPlayer.h
    src/core/MpvPlayer.cpp
    src/core/ThumbnailProbe.h
    src/core/ThumbnailProbe.cpp
    src/core/PlaybackController.h
    src/core/PlaybackController.cpp
    src/core/Theme.h
    src/core/RetroTheme.h
    src/core/ShikimoriClient.h
    src/core/ShikimoriClient.cpp
    src/core/PosterCache.h
    src/core/PosterCache.cpp
    src/core/PosterImageProvider.h
    src/core/PosterImageProvider.cpp
    src/core/PosterThumbnail.h
    src/core/PosterThumbnail.cpp
    src/core/AniListClient.h
    src/core/AniListClient.cpp
    src/core/CatalogBridge.h
    src/core/CatalogBridge.cpp
    src/core/DetailBridge.h
    src/core/DetailBridge.cpp
    src/core/TorrentRanking.h
    src/core/TorrentRanking.cpp
    src/core/AniLibriaClient.h
    src/core/AniLibriaClient.cpp
    src/core/AnimegoClient.h
    src/core/AnimegoClient.cpp
    src/core/KodikClient.h
    src/core/KodikClient.cpp
    src/core/AnimetkaClient.h
    src/core/AnimetkaClient.cpp
    src/core/JacRedClient.h
    src/core/JacRedClient.cpp
    src/core/SukebeiClient.h
    src/core/SukebeiClient.cpp
    src/core/HentaiSiteCommon.h
    src/core/HentaiSiteCommon.cpp
    src/core/HentasisClient.h
    src/core/HentasisClient.cpp
    src/core/AnistarClient.h
    src/core/AnistarClient.cpp
    src/core/StatusStore.h
    src/core/StatusStore.cpp
    src/core/BookmarksBridge.h
    src/core/BookmarksBridge.cpp
)

target_include_directories(anime_client_cpp PRIVATE src ${MPV_INCLUDE_DIR})

target_link_libraries(anime_client_cpp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network Qt6::Sql Qt6::OpenGL Qt6::Concurrent
    ${MPV_LINK_LIBS}
)

if(WIN32)
    target_sources(anime_client_cpp PRIVATE resources/app.rc)
    set_source_files_properties(resources/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/app.ico"
        VS_SETTINGS "ResourceCompileAdditionalIncludeDirectories=${CMAKE_SOURCE_DIR}/resources")
endif()

qt_add_executable(gen_icon tools/gen_icon.cpp)
target_link_libraries(gen_icon PRIVATE Qt6::Gui Qt6::Svg)

set(APP_ICON_SVG "${CMAKE_SOURCE_DIR}/qml/assets/logo-retro.svg")
set(APP_ICON_PNG "${CMAKE_SOURCE_DIR}/resources/app.png")
set(APP_ICON_ICO "${CMAKE_SOURCE_DIR}/resources/app.ico")
set(APP_ICON_QML_PNG "${CMAKE_SOURCE_DIR}/qml/assets/app.png")
add_custom_command(
    OUTPUT ${APP_ICON_PNG} ${APP_ICON_ICO} ${APP_ICON_QML_PNG}
    COMMAND $<TARGET_FILE:gen_icon>
        ${APP_ICON_SVG}
        ${APP_ICON_PNG}
        ${APP_ICON_ICO}
        ${APP_ICON_QML_PNG}
    DEPENDS gen_icon ${APP_ICON_SVG}
    COMMENT "Generating app icon (PNG 512 + multi-size ICO)"
)
add_custom_target(app_icons DEPENDS ${APP_ICON_PNG} ${APP_ICON_ICO} ${APP_ICON_QML_PNG})
add_dependencies(anime_client_cpp app_icons)

# VS F5: рабочая папка = каталог exe (там же Qt DLL, platforms/, qml/).
# Без этого — «отказано в доступе» / не находятся плагины Qt.
set_target_properties(anime_client_cpp PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:anime_client_cpp>"
    VS_DEBUGGER_COMMAND "$<TARGET_FILE:anime_client_cpp>"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT anime_client_cpp)

qt_add_qml_module(anime_client_cpp
    URI AnimeClient
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/HomeView.qml
        qml/BrowseView.qml
        qml/BookmarksView.qml
        qml/SettingsView.qml
        qml/DetailView.qml
        qml/components/Card.qml
        qml/components/CalendarCard.qml
        qml/components/CalendarSection.qml
        qml/components/CalendarRow.qml
        qml/components/CalendarListRow.qml
        qml/components/PillButton.qml
        qml/components/HeroBanner.qml
        qml/components/StatusPill.qml
        qml/components/NavButton.qml
        qml/components/PlayerOverlay.qml
        qml/components/UoscButton.qml
        qml/components/UoscTimeline.qml
        qml/components/PageNavRow.qml
        qml/components/CatalogGrid.qml
        qml/components/SplashScreen.qml
        qml/components/TitleBar.qml
        qml/components/TitleBarButton.qml
        qml/MainRetro.qml
        qml/RetroShell.qml
        qml/RetroTitleBar.qml
        qml/RetroSidebar.qml
        qml/RetroHome.qml
        qml/RetroBrowse.qml
        qml/RetroBookmarks.qml
        qml/RetroSchedule.qml
        qml/RetroSettings.qml
        qml/RetroDetail.qml
        qml/RetroSelect.qml
        qml/RetroCard.qml
        qml/RetroSplashScreen.qml
        qml/ScanlinesOverlay.qml
    RESOURCES
        qml/assets/logo-retro.svg
        qml/assets/player-retro/play.svg
        qml/assets/player-retro/pause.svg
        qml/assets/player-retro/forward-10.svg
        qml/assets/player-retro/rewind-10.svg
        qml/assets/player-retro/volume.svg
        qml/assets/player-retro/subtitles.svg
        qml/assets/player-retro/audio.svg
        qml/assets/player-retro/fullscreen.svg
        qml/assets/player-retro/fullscreen-exit.svg
        qml/assets/player-retro/menu.svg
        qml/assets/player-retro/episode-prev.svg
        qml/assets/player-retro/episode-next.svg
        qml/assets/player-retro/skip-opening.svg
        qml/assets/player-retro/audio-sync.svg
        qml/assets/home.svg
        qml/assets/compass.svg
        qml/assets/bookmark.svg
        qml/assets/settings.svg
        qml/assets/flame.svg
        qml/assets/calendar.svg
        qml/assets/dice.svg
        qml/assets/search.svg
        qml/assets/filters.svg
        qml/assets/clock.svg
        qml/assets/tv.svg
        qml/assets/logo.svg
        qml/assets/app.png
        qml/assets/chevron-left.svg
        qml/assets/chevron-right.svg
        qml/assets/player/menu.svg
        qml/assets/player/play.svg
        qml/assets/player/pause.svg
        qml/assets/player/rewind-10.svg
        qml/assets/player/forward-10.svg
        qml/assets/player/volume.svg
        qml/assets/player/fullscreen.svg
        qml/assets/player/fullscreen-exit.svg
        qml/assets/player/subtitles.svg
        qml/assets/player/audio.svg
        qml/assets/window/minimize.png
        qml/assets/window/close.png
        qml/assets/window/restore.png
        qml/assets/window/maximize.png
        qml/assets/window/maximize.svg
)


# Ниже — Windows-деплой Qt/mpv DLL рядом с exe. На Linux зависимости
# системные (pacman/apt), копировать не нужно.
if(WIN32)

# libmpv-2.dll нужна рядом с exe в рантайме (Qt-деплой через windeployqt
# делается отдельно, эту DLL он не знает).
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPV_DLL}" "$<TARGET_FILE_DIR:anime_client_cpp>"
)

# Автодеплой qt_add_executable копирует только "плоские" DLL рядом с exe,
# но НЕ платформенный плагин (platforms/qwindows[d].dll) — без него Qt не
# может инициализировать оконную систему и падает с "Debug Error! ... no
# Qt platform plugin could be initialized" прямо при старте. Берём нужный
# вариант (debug/release) по конфигурации сборки и кладём в platforms/
# рядом с exe, как ожидает Qt при автопоиске плагинов.
# Qt6_DIR = <vcpkg installed>/x64-windows/share/Qt6 -> на 2 уровня выше
# лежит сам префикс триплета (x64-windows), где живут Qt6/plugins и debug/Qt6/plugins.
set(QT_INSTALL_PREFIX "${Qt6_DIR}/../..")
message(STATUS "Qt deploy prefix: ${QT_INSTALL_PREFIX}")
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/platforms"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/platforms/qwindowsd.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/platforms/qwindows.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/platforms/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/platforms/qwindowsd.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/platforms/qwindows.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/platforms/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/qt.conf"
        "$<TARGET_FILE_DIR:anime_client_cpp>/qt.conf"
)

# Аналогично — Qt6Sql ищет драйвер QSQLITE как плагин в sqldrivers/, а не
# линкует его статически; без этого HistoryManager не может открыть SQLite
# (молча — это не падение, просто прогресс не сохраняется, поэтому легко
# было бы не заметить).
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/sqldrivers"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/sqldrivers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/sqldrivers/qsqlited.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/sqldrivers/qsqlite.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/sqldrivers/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/sqldrivers/qsqlited.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/sqldrivers/qsqlite.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/sqldrivers/"
)

# Qt6Network тоже грузит TLS-движок как плагин (tls/), а не линкует
# статически — без него HTTPS вообще не работает ("No functional TLS
# backend was found", ShikimoriClient не может достучаться до API).
# qschannelbackend — нативный Windows-бэкенд (TLS через WinAPI, без
# отдельных OpenSSL DLL) — деплоим его.
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/tls"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/tls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/tls/qschannelbackendd.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/tls/qschannelbackend.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/tls/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/tls/qschannelbackendd.dll,${QT_INSTALL_PREFIX}/Qt6/plugins/tls/qschannelbackend.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/tls/"
)

# Декодирование изображений (постеры Shikimori — jpeg/webp, иконки — svg)
# тоже плагины (imageformats/), без них QML Image тихо не показывает
# картинку вообще — ни ошибки, ни краша, просто пустой прямоугольник.
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/imageformats"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/imageformats"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/imageformats,${QT_INSTALL_PREFIX}/Qt6/plugins/imageformats>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/imageformats"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/plugins/imageformats,${QT_INSTALL_PREFIX}/Qt6/plugins/imageformats>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/plugins/imageformats"
)

# qsqlite[d].dll сам по себе динамически линкуется с sqlite3.dll — это
# транзитивная зависимость ПЛАГИНА, а не exe, поэтому авто-деплой Qt её не
# подхватывает (он смотрит только на прямые зависимости исполняемого файла).
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/bin/sqlite3.dll,${QT_INSTALL_PREFIX}/bin/sqlite3.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/"
)

# То же самое для qjpeg[d].dll (постеры Shikimori — jpeg) — без jpeg62.dll
# плагин тихо не загружается, и Image выдаёт "Unsupported image format" без
# объяснения причины (см. также Qt6Svg ниже — для qsvg-плагина та же история).
# jpeg62.dll, в отличие от Qt-овских DLL, называется одинаково в debug/release.
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/bin/jpeg62.dll,${QT_INSTALL_PREFIX}/bin/jpeg62.dll>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/"
)

# QtQuick.Controls (и всё, от чего оно зависит — Templates, Window, стили)
# попытка статически слинковать через qt_import_qml_plugins() оказалась
# ненадёжной (CMake генерирует список плагинов от qmlimportscanner, который
# сам появляется только ПОСЛЕ первой сборки — на чистом дереве плагин
# физически не попадал в линковку, и движок падал на "module QtQuick.Controls
# is not installed"). Вместо этого копируем сами QML-плагины Qt (DLL+qmldir)
# рядом с exe и подключаем как runtime import-path (engine.addImportPath в
# main.cpp) — это стандартный, предсказуемый способ деплоя сторонних QML-
# модулей, не зависящий от порядка генерации CMake.
add_custom_command(TARGET anime_client_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anime_client_cpp>/qml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/Qt6/qml/QtQuick,${QT_INSTALL_PREFIX}/Qt6/qml/QtQuick>"
        "$<TARGET_FILE_DIR:anime_client_cpp>/qml/QtQuick"
)

# Те QML-плагины (выше) сами тянут C++-библиотеки Qt, которые НЕ являются
# прямой зависимостью exe (мы не вызываем их функции из кода — это чисто
# QML-уровень) и поэтому не попадают в автодеплой Qt вообще. Без них
# qtquickcontrols2plugin[d].dll падает с "Cannot load library: module not
# found" при попытке Qt загрузить его в рантайме. Список вычислен через
# dumpbin /dependents по всей цепочке QtQuick.Controls -> Basic-стиль.
set(QT_EXTRA_RUNTIME_DLLS
    Qt6QuickControls2
    Qt6QuickTemplates2
    Qt6QuickControls2Basic
    Qt6QuickControls2BasicStyleImpl
    Qt6QuickControls2Impl
    Qt6Svg
    # QtQuick.Effects (MultiEffect — glow/тени в retro-теме, qml/RetroCard.qml
    # и др.) тянет Qt6QuickEffects так же, как Controls тянет свои Impl-либы
    # выше: не прямая C++-зависимость exe, поэтому не попадает в автодеплой.
    Qt6QuickEffects
)
foreach(dll_base ${QT_EXTRA_RUNTIME_DLLS})
    add_custom_command(TARGET anime_client_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${QT_INSTALL_PREFIX}/debug/bin/${dll_base}d.dll,${QT_INSTALL_PREFIX}/bin/${dll_base}.dll>"
            "$<TARGET_FILE_DIR:anime_client_cpp>/"
    )
endforeach()

endif() # WIN32 — конец Windows-деплоя DLL/плагинов

# Linux: install binary + desktop entry (optional prefix)
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS anime_client_cpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${CMAKE_SOURCE_DIR}/resources/app.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
        RENAME anime_client_cpp.png
        OPTIONAL)
endif()

# --- Тесты (не зависящие от сети) ---
# Включаются по умолчанию; отключить: -DBUILD_TESTING=OFF
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
