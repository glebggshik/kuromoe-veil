# KuroMoe Veil

Десктопный клиент аниме на **C++20 + Qt 6 (QML) + libmpv**.

Каталог — Shikimori, озвучки — Kodik / AnimeGO / AniLibria, торренты — JacRed + TorrServer.

## Сборка

Зависимости:

- CMake ≥ 3.21
- Qt 6: Core, Gui, Qml, Quick, Network, Sql, OpenGL, Svg, Concurrent
- libmpv

**Linux**

```bash
cmake -S . -B build
cmake --build build --target anime_client_cpp
./build/anime_client_cpp
```

**Windows**

Нужны Qt 6 (vcpkg) и libmpv (например `C:/dev/libmpv`). Дальше — Visual Studio / Ninja, цель `anime_client_cpp`.

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
