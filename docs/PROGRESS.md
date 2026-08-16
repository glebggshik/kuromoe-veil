# Anime Client (C++/Qt) — прогресс разработки

Документ фиксирует итоги работы над встроенным плеером, UI и API. Актуальный проект: `C:\Users\eblan\Documents\anime_client_cpp` (папка «копия» — устаревшая, не собирать оттуда).

**Обновлено:** 2026-06-29

---

## Сборка и запуск

| Действие | Команда / путь |
|---|---|
| Сборка | `cmake --build build --config Debug --target anime_client_cpp` |
| Альт. сборка | `cmake --build build_vs --config Debug` (если используется `build_vs`) |
| Запуск | `run-debug.bat` — сам ищет exe в `build\Debug` или `build_vs\Debug` |
| **Exe** | `C:\Users\eblan\Documents\anime_client_cpp\build\Debug\anime_client_cpp.exe` |
| Лог | `build\Debug\anime_client.log` (рядом с exe) |

> QML копируется в build при сборке. Без пересборки изменения в плеере не появятся. Перед сборкой закрыть `anime_client_cpp.exe` (иначе LNK1168).

---

## Архитектура (текущая)

```
DetailView.qml
├── DetailBridge          — метаданные, торренты, AniLibria, play()
├── PlaybackController    — поток, история, автопереход серий
└── playerHost
    ├── MpvPlayer (C++)   — libmpv, OpenGL FBO + SW fallback
    └── PlayerOverlay.qml — uosc-style UI поверх видео

Main.qml                  — ApplicationWindow, нижняя навигация, FullScreen в cinemaMode
├── HomeView              — каталог, поиск, фильтры
├── BrowseView            — популярное / новое / онгоинг
├── BookmarksView         — списки по статусу (StatusStore)
└── SettingsView          — настройки (embedded)
```

**Ключевые C++**

| Файл | Назначение |
|---|---|
| `src/core/MpvPlayer.cpp` | Плеер, рендер, дорожки, `m_preferredAudioTitle` |
| `src/core/PlaybackController.cpp` | Воспроизведение, история, торренты |
| `src/core/HistoryManager.cpp` | SQLite, автосохранение позиции |
| `src/core/DetailBridge.cpp` | Торренты, AniLibria, Kodik, `play()` |
| `src/core/KodikClient.cpp` | Озвучки Kodik, HLS-ссылки (прокси) |
| `src/core/ShikimoriClient.cpp` | GraphQL, `getByIds`, `bestPosterUrl()` |
| `src/core/PosterCache.cpp` | Очередь скачивания постеров на диск, `preloadCatalog()` |
| `src/core/PosterThumbnail.cpp` | Миниатюры без QML `Image` (async decode, in-memory) |
| `src/core/AniListClient.cpp` | `heroBanner` для Hero (bannerImage) |
| `src/core/BookmarksBridge.cpp` | Закладки + обогащение из API |
| `src/core/StatusStore.cpp` | SQLite: статусы, постеры, торренты |

**Ключевые QML**

| Файл | Назначение |
|---|---|
| `qml/components/PlayerOverlay.qml` | Проксимити UI, контролы, список серий, дорожки |
| `qml/components/UoscTimeline.qml` | Таймлайн, seek preview |
| `qml/components/UoscButton.qml` | Кнопки uosc (SVG / текст) |
| `qml/DetailView.qml` | Экран тайтла, cinema layer, `maxEpisode` → оверлей |
| `qml/Main.qml` | Навигация, `cinemaMode` → `Window.FullScreen` |
| `qml/components/Card.qml` | Карточки каталога через `PosterThumbnail` |
| `qml/components/HeroBanner.qml` | Hero: QML `Image` + AniList-баннер, `PreserveAspectCrop` |

---

## Что сделано

### 1. Встроенный плеер и экран тайтла

- Плеер в `DetailView`, не отдельное OS-окно.
- `PlaybackController` + `MpvPlayer` + `DetailBridge`.
- История в SQLite, промпт «продолжить с …».
- Fullscreen через `Main.qml` (`Window.FullScreen` + `cinemaLayer`), не отдельное `Window` (ломает GL mpv).

### 2. Оверлей плеера (uosc-style)

- Проксимити-зоны: верх / низ / громкость / список серий (sticky ~10 px).
- Колесо: громкость; Shift+колесо: ±10 с; над списком серий / дорожек — скролл списка.
- Flash-индикаторы: пауза, громкость, seek, скорость, дорожка (озвучка/CC).
- Правый клик — закрепить UI (`uiPinned`).

### 3. Нижняя панель контролов (2026-06-29)

Три зоны в одной строке над таймлайном:

| Зона | Содержимое |
|---|---|
| **Слева** | Бургер (список серий) + название + время |
| **По центру** | `‹` · play/pause · `›` (переключение серий) |
| **Справа** | −10, +10, озвучка, субтитры, скорость, fullscreen |

Иконки центра: `chevron-left/right.svg`, `play.svg` / `pause.svg` из `qml/assets/player/`.

### 4. Список серий (бургер)

- Выпадающий `ListView` слева над контролами (не в центральной строке).
- `maxEpisode` из `DetailView.currentMaxEpisode` (учёт озвучки / AniLibria).
- Свойство `effectiveMaxEpisode` — fallback на `playback.totalEpisodes`.
- **Исправлен баг якоря:** `episodeListPanel` больше не якорится к `bottomBlock` (не sibling) → `anchors.bottom: parent.bottom`.
- **Исправлен баг кнопки «следующая серия»:** было `parent.maxEp` у `Row`, стало `root.effectiveMaxEpisode`.
- `onRequestEpisode` в `DetailView` синхронизирует `root.episode` перед `bridge.play()`.

### 5. Озвучка и субтитры по центру (2026-06-29)

Вместо `Popup` у кнопок внизу:

- Панель **по центру видео** (стиль как scrub/volume: `#90000000`, рамка).
- Заголовок «Озвучка» / «Субтитры», список, «Выкл» для субтитров.
- Клик мимо — закрыть; колесо — скролл списка.
- После выбора — flash: `🔊 …` / `CC …` (`trackFlash`).

### 6. Полноэкранный: двойной клик (2026-06-29)

- **Двойной ЛКМ** по видео → `cinemaMode` / `Window.FullScreen`.
- **Одиночный ЛКМ** → пауза/плей с задержкой **65 мс** (`leftClickTimer`), чтобы не путать с double-click.
- При double-click таймер одиночного клика останавливается.

**Как у других плееров (для справки):**

| Плеер | Поведение |
|---|---|
| **mpv + uosc** | Разные биндинги `MBTN_LEFT` / `MBTN_LEFT_DBL`; интервал `--double-click-time` ~300 мс |
| **VLC pause-click** | Пауза откладывается на всё окно double-click |
| **YouTube** | Один клик — play; двойной по центру — fullscreen; по бокам — ±10 с |
| **У нас** | Компромисс: 65 мс (быстрее классики ~300 мс); при очень быстрых кликах возможен ложный fullscreen |

### 7. Закладки и Hero (GraphQL + постеры)

**Проблема:** пустые обложки в закладках и Hero.

**Причина:** в `ShikimoriClient::buildQuery()` `limit` из `ids.size()` сериализовался как строка `"3"` → ошибка API `3 is not a positive integer`.

**Исправления:**

- Корректная сериализация `int` / `LongLong` в `buildQuery()`.
- `getByIds()`: `limit` как `int`.
- `bestPosterUrl()` — предпочтение jpeg (Qt без webp-плагина).
- `BookmarksBridge` — обогащение карточек из API.

### 8. MpvPlayer: озвучка между сериями

- `m_preferredAudioTitle` — запоминает выбранную дорожку при смене серии.

### 9. Иконка приложения (2026-06-29)

- Лого из `qml/assets/logo.svg` → `resources/app.ico` + `qml/assets/app.png`.
- **Панель задач и заголовок окна:** `resources/app.rc` вшивает `.ico` в exe (Windows).
- **Qt:** `QGuiApplication::setWindowIcon()` + `QQuickWindow::setIcon()` в `main.cpp` (в QML `ApplicationWindow` нет `icon`).
- Перегенерация: `build\Debug\gen_icon.exe` → `resources/app.png`, затем Pillow → `app.ico`, копия в `qml/assets/app.png`.

### 10. Kodik (2026-06-29)

- `KodikClient` — порт `video_source.py` / `anime_parsers_ru.KodikParser`.
- Озвучки по `shikimori_id` → `translationsReady` в `DetailView`.
- `DetailBridge::play()` → `getEpisodeStream()` → `playDirectUrl(..., useProxy=true)`.
- Токен кэшируется в `AppConfig`; геоблок — прокси из настроек.
- UI: вкладка источника **Kodik** + список озвучек (приоритет над AniLibria/Torrent).

### 11. Таймлайн, производительность, навигация

- Единый тёмный низ + `UoscTimeline` (прогресс внутри трека).
- GPU FBO + SW fallback, throttling position/redraw.
- Настройки рендера/FPS в `SettingsView`.
- KuroMoe Veil: нижняя pill-навигация, Browse, Bookmarks.
- Торренты: поиск по `originalTitle` (JacRed).
- Стабильность: `MpvCallbackBridge`, `pop` вместо `clear` в StackView, shutdown mpv.

### 12. Постеры, Hero, плавная навигация (2026-06-29, вечер)

**Проблемы:** Hero без картинки; переключение вкладок 2–4 с; каталог без постеров в Debug; краш календаря; пост-exit `0xC0000005`; TorrServer 404; потеря последнего столбца сетки; микрофриз на «Закладках».

**Архитектура постеров**

| Компонент | Роль |
|---|---|
| `PosterCache` | Скачивает `https://` постеры на диск (`CacheLocation/posters`), очередь 4 параллельно, high-priority для hero |
| `PosterThumbnail` | `QQuickPaintedItem` вместо QML `Image` в карточках — async decode (`QtConcurrent`), без abort в MSVC Debug |
| `HeroBanner` | Отдельно: QML `Image` + `asynchronous` + `PreserveAspectCrop` — широкие баннеры AniList (`.jpg`) |

**Hero**

- Источник: `heroBanner` (AniList) → `posterHd` → `poster` (Shikimori).
- Не через `PosterThumbnail` — иначе очередь каталога и нет crop.
- `CatalogBridge::emitHeroWithBanners()` + ротация `heroSlides` для популярного.

**Каталог (Home / Browse)**

- `Flow`+`Repeater` заменён на `GridView` (`cacheBuffer: 800`) — только видимые карточки.
- `contentWidth = gridColumns × 166` (раньше `−6` для Flow — терялся последний столбец).
- `posterCache.preloadCatalog(items)` при каждом `resultsReady` — фоновое кэширование на диск.

**Навигация между вкладками**

- Все 4 вкладки снова живут в `tabStrip` постоянно (slide 220 ms), без ленивых `Loader`.
- Предзагрузка при старте: Browse `ensureLoaded()` через 400 ms, Bookmarks `ensureLoaded()` через 1200 ms.
- `PosterThumbnail`: при `posterActive=false` картинка **остаётся в памяти** (не сбрасывается) — мгновенный возврат на вкладку.
- Закладки: `ensureLoaded()` один раз, без `reload()` при каждом клике на иконку; `GridView` вместо `Flow`.
- Закладки: `warmPosters` — декод видимых постеров в фоне, пока пользователь на другой вкладке.

**Прочие фиксы сессии**

- GraphQL Shikimori: поля постера `previewUrl` / `main2xUrl` / … (не `x96Url`).
- Календарь Debug: текстовые строки без массовых QML `Image`; GraphQL-enrich отключён в `#ifndef NDEBUG`.
- `quick_exit` в Debug после `app.exec()` — обход краша деструкторов.
- Platform plugin / `QT_PLUGIN_PATH` в `main.cpp`; иконка через `app.rc` + `gen_icon`.
- TorrServer: авто re-add magnet при HTTP 404.
- Центрирование pill-кнопок Home и Browse.

**Splash-экран при запуске**

- `SplashScreen.qml` — фон `#0a0a0f`, логотип, «KuroMoe Veil» (Bahnschrift / Segoe UI Light).
- Пока splash виден — chrome скрыт; под ним грузятся Home + Browse + Bookmarks.
- Скрытие: минимум 1.4 с **и** все три вкладки ответили (каталог + hero + обзор + закладки); таймаут 9 с.
- Фоновый `warmPosters` на закладках после preload.

**Известный остаток**

- Микрофриз на «Закладках», если открыть вкладку в первую секунду после splash (до `warmPosters`).

---

## PlayerOverlay.qml — шпаргалка

```
Слои (z):
  1  — MouseArea ввода (клик/double-click/колесо)
  3–4 — topBar, volumeBar, bottomChrome, timeline
  9  — episodeListZone
  11 — trackPickerLayer (озвучка/субтитры)
  10+ — flash-индикаторы

Сигналы:
  requestEpisode(ep)  → DetailView → bridge.play(ep, tid)
  requestCinemaToggle → DetailView.cinemaMode → Main.FullScreen

Свойства от DetailView:
  maxEpisode: currentMaxEpisode
  playback, player, titleText, cinemaMode
```

---

## Известные ограничения

- Kodik требует **прокси** в настройках (геоблок); без него озвучки/потоки недоступны.
- WebP-постеры: fallback на `.jpg`; без `qwebp` в Qt возможны warning в логе.
- Режим рендера в настройках — перезапуск приложения.
- Быстрые клики pause-play-pause-play могут попасть в double-click Qt → fullscreen (редко при 65 мс).
- Папка `anime_client_cpp — копия` не синхронизирована с основным проектом.
- «Закладки»: лёгкий фриз, если открыть вкладку раньше ~1.2 с после старта (до фонового `warmPosters`).

---

## Будущие шаги

### Плеер / UX

- [ ] Горячие клавиши (Space, F, M, стрелки) в `cinemaLayer`.
- [ ] Ужесточить double-click (клики в одну точку или порог <100 мс) при жалобах на ложный fullscreen.
- [ ] Миниатюры на таймлайне (thumbfast / mpv).
- [ ] Runtime fallback GPU → SW по «чёрному кадру».

### Приложение
- [ ] WebP: Qt с `qwebp` или серверный прокси постеров.
- [ ] CI / Release-сборка.
- [ ] Unit-тесты `HistoryManager`, `EpisodeParser`.

### Сделано (отметки)

- [x] Нижняя навигация + Browse + Bookmarks (2026-06-28)
- [x] Краш `0xC0000005` при tab switch с плеером (2026-06-28)
- [x] Центральные контролы `‹ play ›` (2026-06-29)
- [x] Список серий + фиксы загрузки/переключения (2026-06-29)
- [x] Озвучка/субтитры по центру + track flash (2026-06-29)
- [x] Double-click → fullscreen, delay 65 мс (2026-06-29)
- [x] GraphQL limit + постеры закладок/Hero (2026-06-29)
- [x] Иконка exe / панель задач / заголовок окна (2026-06-29)
- [x] Kodik + прокси в `DetailBridge::play` — `KodikClient`, озвучки, HLS (2026-06-29)
- [x] `PosterCache` + `PosterThumbnail`, `GridView`, preload постеров (2026-06-29)
- [x] Hero: AniList-баннер через QML `Image` (2026-06-29)
- [x] Плавная навигация вкладок, фикс сетки и закладок (2026-06-29)
- [x] Splash-экран загрузки при старте (2026-06-29)

---

## Рекомендуемые настройки

1. **Рендер:** Авто (GPU → CPU fallback).
2. **FPS:** Авто или «Без лимита» на 120 Гц.
3. После смены рендера — перезапуск `run-debug.bat`.
4. При чёрном экране — «Программный (CPU)», FPS 30.

---

## Файлы для правок плеера

```
qml/components/PlayerOverlay.qml   — весь UI плеера
qml/components/UoscTimeline.qml
qml/components/UoscButton.qml
qml/DetailView.qml                 — cinemaMode, maxEpisode, bridge.play
qml/Main.qml                       — FullScreen при cinemaActive
src/core/MpvPlayer.{h,cpp}         — дорожки, preferred audio
src/core/PlaybackController.{h,cpp}
src/core/DetailBridge.{h,cpp}
src/core/KodikClient.{h,cpp}
src/core/ShikimoriClient.{h,cpp}   — GraphQL, постеры
src/core/PosterCache.{h,cpp}       — дисковый кэш постеров
src/core/PosterThumbnail.{h,cpp}   — миниатюры в карточках
qml/components/HeroBanner.qml      — hero-баннер (не PosterThumbnail)
docs/PROGRESS.md                   — этот файл
```

---

*Дальнейшие правки плеера — в `anime_client_cpp`, сборка `cmake --build build --config Debug`, проверка через `run-debug.bat`.*