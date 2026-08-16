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

### 1. Гонка async при смене серии — сделано
`DetailBridge::m_playGen` инкрементируется на каждый `play()` / `playSmashMixed()`; колбэки `getEpisodeStream` игнорируют stale `gen`.

### 2. Smash + авто-переход на следующую серию — сделано
`PlaybackController::setSmashAudioHint` + сигнал `smashNextEpisodeNeeded` → `DetailBridge` снова зовёт `playSmashMixed`.

### 3. SQLite: WAL + busy_timeout — сделано
`HistoryManager` и `StatusStore` при `openDatabase()`:

```sql
PRAGMA journal_mode=WAL;
PRAGMA busy_timeout=5000;
PRAGMA synchronous=NORMAL;
```

### 4. Нормальный shutdown вместо `std::quick_exit` — сделано
Синглтоны живут как `new T(qApp)` (дети QGuiApplication, умирают до неё). `Theme`/`RetroTheme` — локалы `main`, не `static`. `aboutToQuit`: flush истории → join thread pool → `NetworkManager::shutdown()`. `main` делает обычный `return`.

---

## P2 — инфра и гигиена

### 15. Unit-тесты (то, что не зависит от сети)
**Сделано:** `EpisodeParser` — `tests/test_episode_parser.cpp` (пойман и починен баг
`S01E05`); ранжирование/поиск торрентов вынесены в `TorrentRanking.{h,cpp}` и покрыты
(`tests/test_torrent_ranking.cpp`); `HistoryManager` — `tests/test_history_manager.cpp`
(temp SQLite; заодно `mostRecent()` получил детерминированный tie-break по rowid —
`updated_at` с точностью до секунды). Всё подключено к `ctest` (`-DBUILD_TESTING=OFF`
отключает), 3 теста зелёные.

**Осталось:**
| Модуль | Почему сложнее |
|--------|----------------|
| Kodik HTML parse | нужны fixtures из `tools/` dumps + вынос парсера из `KodikClient.cpp` |

**Размер:** M

---

## P3 — полировка

| # | Что | Зачем | Размер |
|---|-----|-------|--------|
| 20 | Thumbnails на таймлайне (thumbfast/mpv) | удобство seek | L |
| 21 | Runtime fallback GPU → SW при «чёрном кадре» | железо без нормального GL | M |
| 22 | Hot-switch темы без restart | комфорт; не обязательно | L |
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

### Быстрый пакет — сделано
1. Play-generation на async stream resolve  
2. Smash auto-next с audio  
3. SQLite WAL  
4. Shutdown без `quick_exit`  
5. Git + `.gitignore`

### Стабильность (следующая неделя)
6. Async TorrServer probe  
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

- [x] Быстрое переключение серий 1→5→2 не запускает «не ту» после resolve  
- [x] Smash: EOF → следующая серия с тем же audio source  
- [x] WAL + busy_timeout на общем `history.sqlite3`  
- [x] Закрытие окна: обычный `return` из `main`, без `std::quick_exit`

---

*Только план. Реализация — отдельным заходом по пунктам.*
