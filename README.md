# KuroMoe Veil

Десктопный клиент аниме на **C++20 + Qt 6 (QML) + libmpv**.

Каталог — Shikimori, озвучки — Kodik / AnimeGO / AniLibria, торренты — JacRed + TorrServer.

## Сборка

Зависимости: CMake ≥ 3.21, **три** модуля Qt 6 (не весь Qt) и libmpv.

| Нужно | Зачем |
|--------|--------|
| **qtbase** | Core, Gui, Network, Sql/SQLite, OpenGL, Concurrent |
| **qtdeclarative** | Qml, Quick, Quick Controls |
| **qtsvg** | иконки SVG + `gen_icon` |

Не ставь `qt`, `qt6` целиком, `qtwebengine`, `qtmultimedia` — они не линкуются.

**Arch / CachyOS**

```bash
sudo pacman -S --needed cmake ninja pkgconf qt6-base qt6-declarative qt6-svg mpv
cmake -S . -B build
cmake --build build --target anime_client_cpp
./build/anime_client_cpp
```

**Debian / Ubuntu**

```bash
sudo apt install cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev libqt6svg6-dev libmpv-dev
```

**Установка (Linux, «собрал — в меню»)**

Три команды — и ярлык появляется в меню:

```bash
git clone https://github.com/glebggshik/kuromoe-veil.git
cd kuromoe-veil
cmake -S . -B build
cmake --build build
cmake --install build --prefix $HOME/.local
```

Что получается:

| Путь | Что |
|------|-----|
| `~/.local/bin/anime_client_cpp` | бинарь |
| `~/.local/share/applications/kuromoe-veil.desktop` | ярлык меню (Exec — на установленный бинарь) |
| `~/.local/share/pixmaps/kuromoe-veil.png` | иконка |

`gen_icon` запускать не нужно — иконка берётся из `resources/app.png`.

Если меню не видит ярлык сразу:

```bash
update-desktop-database ~/.local/share/applications 2>/dev/null || true
```

Переустановка (после пересборки): достаточно повторить `cmake --build build && cmake --install build --prefix $HOME/.local` — ярлык перезапишется с актуальным путём.

**Windows (vcpkg, только эти порты)**

```bat
vcpkg install --triplet x64-windows
```

Манифест `vcpkg.json` тянет `qtbase` + `qtdeclarative` + `qtsvg`. libmpv отдельно: готовая dev-сборка в `C:/dev/libmpv` (shinchiro).

Если libmpv лежит в другом месте — переопредели при конфигурации (или через env `MPV_ROOT_HINT` / `MPV_ROOT`):

```bat
cmake -S . -B build -DMPV_ROOT_HINT=C:/путь/к/libmpv ^
      -DMPV_DLL=C:/путь/к/libmpv/bin/libmpv-2.dll
```

CMake ищет `include/mpv/client.h`, `lib/mpv` и `bin/libmpv-2.dll` относительно `MPV_ROOT_HINT`; в крайнем случае задай `-DMPV_INCLUDE_DIR=... -DMPV_LIBRARY=...` напрямую.

**Windows-релиз (одна команда, на Windows-машине с MSVC/vcpkg/windeployqt):**

```bat
packaging\windows\package-release.bat
```

Скрипт: vcpkg (манифест) → cmake → сборка Release → `windeployqt` → копия `libmpv-2.dll` → `dist\kuromoe-veil-win64.zip` (портабельный: exe + Qt-плагины + libmpv). Переменные `VCPKG_ROOT` / `MPV_ROOT` переопределяют пути (по умолчанию `C:\dev\vcpkg` и `C:\dev\libmpv`).

Подробности — в [docs/MANUAL.md](docs/MANUAL.md).

## Настройки

В приложении (и в `config.ini`):

| Ключ | Зачем |
|------|--------|
| JacRed URL | Поиск торрентов, по умолчанию `https://jac.red` |
| Kodik token | Публичный токен вшит в код. Свой — только если дефолт перестал работать |
| Прокси | SOCKS5/HTTP для Kodik / AnimeGO, если геоблок |

Образец без личных данных: `packaging/config.ini.example`.

## Что не коммитить

Сборки (`build/`, `dist/`), логи, `terminals/`, живой `config.ini` с прокси.
