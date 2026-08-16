# KuroMoe Veil — что поправить

Личный проект. Документ — backlog по итогам аудита кода (без правок в коде).  
Две темы (classic / retro) — **задумано**, не техдолг.

**Дата:** 2026-07-09

---

## Приоритет

| Приоритет | Смысл |
|-----------|--------|
| P0 | Баги / целостность данных / краши |
| P1 | Стабильность, UX, меньше «тихо сломалось» |
| P2 | Инфра, тесты, гигиена репо |
| P3 | Полировка, когда остальное спокойно |

Оценка: **S** — часы / 1 день · **M** — 2–5 дней · **L** — неделя+

---

## P0 — обязательно

### 1. Гонка async при смене серии
**Где:** `DetailBridge::play` / `playSmashMixed` (Kodik, CVH, Hentasis, AniStar)

`PlaybackController` отменяет устаревшие колбэки через `m_playGeneration`, но резолв URL (`getEpisodeStream`) — нет. Быстрый клик «серия 5 → серия 3» может запустить 5 после 3.

**Что сделать:** generation/token на каждый `play()`; в лямбдах `getEpisodeStream` игнорировать stale-ответ.

**Размер:** S

---

### 2. Smash + авто-переход на следующую серию
**Где:** `PlaybackController::onPlayerEndOfFile` + QML (`DetailView` / `RetroDetail`)

Для торрента контроллер сам зовёт `playTorrentEpisode` **без** повторного `attachExternalAudio` / `playSmashMixed`. После EOF в режиме Smash остаётся только видео с торрента.

**Что сделать:** при EOF в smash снова вызывать `playSmashMixed` (или сохранять audio translation id и re-attach).

**Размер:** S

---

### 3. SQLite: WAL + busy_timeout
**Где:** `HistoryManager`, `StatusStore` (один `history.sqlite3`, два connection)

Нет `PRAGMA journal_mode=WAL`, нет `busy_timeout`. Риск `SQLITE_BUSY` и порчи при kill/crash.

**Что сделать:** при `openDatabase()`:

```sql
PRAGMA journal_mode=WAL;
PRAGMA busy_timeout=5000;
PRAGMA synchronous=NORMAL;
```

**Размер:** S

---

### 4. Нормальный shutdown вместо `std::quick_exit`
**Где:** `main.cpp`

`quick_exit` обходит краш `0xC0000005` на деструкторах синглтонов после `QGuiApplication`. Flush/join делается вручную в `aboutToQuit`, но lifetime объектов всё ещё кривой.

**Что сделать:**
1. Явный `AppShutdown`: History flush → Mpv stop → TorrServer shutdown → NetworkManager → thread pool.
2. Синглтоны с контролируемым временем жизни (не «умирают после QGuiApplication»).
3. Убрать `std::quick_exit`, когда exit стабилен в Debug и Release.

**Размер:** M–L

---

## P1 — стабильность и UX

### 5. Убрать `QEventLoop` с GUI-потока
**Где:** `TorrentStreamManager::isServerResponding`, синхронные куски `PosterCache`

Блокировка event loop → микрофризы при старте TorrServer / картинках.

**Что сделать:** полностью async (callback / signal), без `loop.exec()` на main thread.

**Размер:** M

---

### 6. Generation / cancel при уходе с экрана деталей
**Где:** `DetailBridge::load` и цепочки Kodik/CVH/JacRed/…

При быстром открытии нескольких тайтлов копятся in-flight запросы (лишний трафик, риск rate-limit/ban).

**Что сделать:** уже есть gen для CVH/Kodik/torrents — проверить, что **все** колбэки (AniLibria, related, AniList, hentai) жёстко режутся по gen; при `load` нового id — cancel/ignore старых.

**Размер:** S–M

---

### 7. Smash: не молчать при ошибках audio
**Где:** `DetailBridge::playSmashMixed`

Если нет `animegoId` / пустой URL — тихий `return`. Видео играет без озвучки без сообщения.

**Что сделать:** `emit error(...)` с понятным текстом.

**Размер:** S

---

### 8. `StreamReadiness` — правильный Referer
**Где:** `StreamReadiness.cpp` (сейчас захардкожен `https://animego.org/`)

Для probe не-HLS/не-CVH ссылок referer может быть неверным → ложный fail.

**Что сделать:** передавать referer/route с вызывающей стороны (как в `MpvPlayer::playUrl`).

**Размер:** S

---

### 9. Горячие клавиши в cinema mode
**Где:** `PlayerOverlay` / cinema layer (есть в `docs/PROGRESS.md` как TODO)

Space — play/pause, F — fullscreen, M — mute, ←/→ — seek, ↑/↓ — volume.

**Размер:** S–M

---

### 10. Double-click → fullscreen (ложные срабатывания)
**Где:** `PlayerOverlay` (delay ~65 ms)

Быстрые pause/play могут уйти в fullscreen.

**Что сделать:** ужесточить (порог расстояния курсора / ~200–300 ms как у mpv/uosc) — если начнёт бесить.

**Размер:** S

---

### 11. Ошибки источников в UI, не только в логе
**Где:** `DetailBridge` loadKodik/loadCvh + QML

В логе: empty Kodik, CVH failed; в UI часто пустой список без «нужен прокси / не найдено».

**Что сделать:** per-source статус: loading / ok / empty / error(+hint про прокси для Kodik).

**Размер:** M

---

### 12. TorrServer host/port из настроек
**Где:** `TorrentStreamManager::host()` → `http://127.0.0.1:8090`

**Что сделать:** `AppConfig` + поле в Settings (default 8090).

**Размер:** S

---

### 13. Debug ≠ Release (календарь)
**Где:** `ShikimoriClient` — `#ifndef NDEBUG` отключает `enrichCalendarDays`

В Debug нет постеров/фильтра «скрывать китайские».

**Что сделать:** починить root cause abort в Debug **или** явно писать в UI «урезанный календарь (Debug)». Лучше починить.

**Размер:** M

---

## P2 — инфра и гигиена

### 14. Git
Репозитория нет. Для личного проекта всё равно полезно (откат, diff, бэкап истории).

**Что сделать:**
```
git init
```
`.gitignore` минимум:
```
/build/
/build_vs/
/dist/
/terminals/
/agent-tools/
*.log
*.sqlite3
.vs/
CMakeUserPresets.json
```

**Размер:** S

---

### 15. Unit-тесты (то, что не зависит от сети)
**Кандидаты:**
| Модуль | Почему легко |
|--------|----------------|
| `EpisodeParser` | чистые строки → int |
| torrent relevance / query builders | вынести из анонимного namespace `DetailBridge.cpp` |
| `HistoryManager` | temp SQLite file |
| Kodik HTML parse | fixtures из `tools/` dumps |

CMake target + Qt Test / Catch2. Без CI сначала — `ctest` локально.

**Размер:** M

---

### 16. Почистить мусор в дереве (не исходники)
| Путь | Действие |
|------|----------|
| `terminals/` | удалить / в `.gitignore` |
| `agent-tools/` | удалить |
| лишние `dist/*_1`, старые zip | оставить один актуальный релиз |
| `qml/sandbox/` | оставить для экспериментов; не тащить в packaging (уже не в `QML_FILES`) |
| `tools/*.py` probes | оставить; опционально `tools/README.md` «это не runtime» |

**Размер:** S

---

### 17. CMake / deploy
Ручной `POST_BUILD` copy плагинов Qt/mpv хрупкий, но работает.

**Что сделать (когда будет время):** `windeployqt` + явный copy `libmpv-2.dll`; document `MPV_ROOT` / env вместо только `C:/dev/libmpv`.

**Размер:** M

---

### 18. Пароль прокси в `config.ini`
Plaintext. Для личного ПК ок; для шаринга папки — нет.

**Что сделать (опционально):** Windows DPAPI / не писать password в portable zip (packaging уже кладёт example без секретов — проверить, что так и останется).

**Размер:** S

---

## P3 — полировка

| # | Что | Зачем | Размер |
|---|-----|-------|--------|
| 19 | WebP-плагин / единый jpeg-path | меньше warning и пустых постеров | S |
| 20 | Thumbnails на таймлайне (thumbfast/mpv) | удобство seek | L |
| 21 | Runtime fallback GPU → SW при «чёрном кадре» | железо без нормального GL | M |
| 22 | Hot-switch темы без restart | комфорт; не обязательно | L |
| 23 | `attachPlayer`: disconnect старого player | защита на будущее | S |
| 24 | Версия схемы SQLite + миграции | будущие ALTER без боли | S |
| 25 | Health-check индикатор (прокси / TorrServer / Kodik) | диагностика «почему не играет» | M |

---

## Не трогать без нужды

- Две темы (classic + retro) и параллельные QML/assets — так задумано.
- Dual `QNetworkAccessManager` (proxy vs local) — правильно.
- Generation tokens в TorrServer / CVH / Kodik **load** — оставить, расширять.
- `MpvSharedState` + mutex — осознанная защита от use-after-free.
- `PosterCache` + `PosterThumbnail` — решают реальные проблемы Debug/перфа.
- Retry/jitter Kodik и JacRed — нужны против DDoS-Guard / 429.
- Packaging: не класть личный `config.ini` / `history.sqlite3` в zip.

---

## Предлагаемый порядок работ

### Быстрый пакет (2–4 дня)
1. Play-generation на async stream resolve  
2. Smash auto-next с audio  
3. SQLite WAL  
4. Git + `.gitignore` + подчистить `terminals/`  
5. Ошибки Smash / понятнее empty sources в UI (минимум)

### Стабильность (следующая неделя)
6. Shutdown без `quick_exit`  
7. Async TorrServer probe  
8. Unit-тесты `EpisodeParser` + ranking  
9. Hotkeys  

### Когда станет скучно
10. Deploy/CMake, WebP, timeline thumbs, health UI  

---

## Файлы-якоря

```
src/main.cpp                         — quick_exit, shutdown
src/core/DetailBridge.{h,cpp}        — play, smash, sources
src/core/PlaybackController.{h,cpp}  — EOF, generation, torrent next
src/core/HistoryManager.cpp          — SQLite progress
src/core/StatusStore.cpp             — SQLite statuses
src/core/TorrentStreamManager.cpp    — QEventLoop, host
src/core/StreamReadiness.cpp         — probe / referer
src/core/KodikClient.cpp             — fragile scraper
src/core/MpvPlayer.cpp               — player lifetime
qml/DetailView.qml                   — classic detail + play
qml/RetroDetail.qml                  — retro detail + play
qml/components/PlayerOverlay.qml     — hotkeys, double-click
docs/PROGRESS.md                     — уже известные TODO
test-checklist.md                    — ручной smoke
```

---

## Критерий «готово» для P0

- [ ] Быстрое переключение серий 1→5→2 не запускает «не ту» после resolve  
- [ ] Smash: EOF → следующая серия с тем же audio source  
- [ ] После kill/force-close прогресс не теряется чаще, чем autosave 5 с; БД не ломается  
- [ ] Закрытие окна во время плеера: exit code `0`, без `0xC0000005` **без** `quick_exit` (или с ним только как временный fallback, задокументированный)

---

*Только план. Реализация — отдельным заходом по пунктам.*
