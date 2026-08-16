#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <csignal>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QtGlobal>
#include <QIcon>
#include <QQuickWindow>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#ifdef _DEBUG
#ifdef _WIN32
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif
#endif

#include "core/AppConfig.h"
#include "core/BookmarksBridge.h"
#include "core/CatalogBridge.h"
#include "core/DetailBridge.h"
#include "core/HistoryManager.h"
#include "core/MpvPlayer.h"
#include "core/NetworkManager.h"
#include "core/PlaybackController.h"
#include "core/PosterCache.h"
#include "core/PosterImageProvider.h"
#include "core/PosterThumbnail.h"
#include "core/RetroTheme.h"
#include "core/Theme.h"

namespace {
QMutex g_logMutex;
QFile *g_logFile = nullptr;

void installFileLogger() {
    const QString path = QCoreApplication::applicationDirPath() + "/anime_client.log";
    g_logFile = new QFile(path);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        QMutexLocker lock(&g_logMutex);
        if (!g_logFile)
            return;
        const char *level = "INFO";
        switch (type) {
        case QtDebugMsg: level = "DEBUG"; break;
        case QtWarningMsg: level = "WARN"; break;
        case QtCriticalMsg: level = "CRIT"; break;
        case QtFatalMsg: level = "FATAL"; break;
        case QtInfoMsg: level = "INFO"; break;
        }
        QTextStream out(g_logFile);
        out.setEncoding(QStringConverter::Utf8);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " [" << level << "] ";
        if (ctx.function)
            out << ctx.function << ": ";
        out << msg << '\n';
        out.flush();
#ifdef _DEBUG
        fprintf(stderr, "[%s] %s\n", level, qUtf8Printable(msg));
        fflush(stderr);
#endif
    });
}

void installCrashTrace() {
    std::signal(SIGABRT, [](int) {
        fprintf(stderr, "[CRASH] SIGABRT — см. anime_client.log\n");
        fflush(stderr);
        std::_Exit(3);
    });
}
}

int main(int argc, char *argv[]) {
#ifdef _DEBUG
#ifdef _WIN32
    // WIN32-приложение без консоли — при запуске из run-debug.bat ошибки
    // QML не видны; подключаем stdout/stderr к уже открытому cmd.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *out = nullptr;
        FILE *err = nullptr;
        freopen_s(&out, "CONOUT$", "w", stdout);
        freopen_s(&err, "CONOUT$", "w", stderr);
        // Лог/stderr пишем в UTF-8 (qUtf8Printable) — переключаем консоль,
        // иначе кириллица в cmd (CP866) отображается мусором.
        SetConsoleOutputCP(CP_UTF8);
    }
#endif
#endif

    // Без явного стиля QtQuick.Controls на старте пробует ВСЕ опциональные
    // стили (Material/Fusion/Universal/Windows/...), и поломка ЛЮБОГО из них
    // (не задеплоен целиком — мы тянем только Basic) валит загрузку всего
    // приложения, хотя сам стиль не используется. Basic — единственный,
    // под который реально задеплоены зависимости (см. CMakeLists.txt).
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    // Лимит кэша декодированных картинок Qt (КБ). По умолчанию раздувается
    // при каталоге/банерах и не сбрасывается в «простое».
    qputenv("QT_IMAGEIO_MAXALLOC", "128"); // МБ на один decode
    // Иначе любой QML Warning (в т.ч. ReferenceError) в debug = abort().
    qunsetenv("QT_FATAL_WARNINGS");
    // Остаток QT_QPA_PLATFORM=offscreen (тесты/IDE) → "no platform plugin" при старте.
    qunsetenv("QT_QPA_PLATFORM");
    qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
    qunsetenv("QT_PLUGIN_PATH");

    QCoreApplication::setOrganizationName("AnimeClient");
    QCoreApplication::setApplicationName("AnimeClientCpp");

    // QQuickFramebufferObject требует OpenGL scene graph (и для GPU-, и для SW-mpv).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Пути к плагинам (platforms/, plugins/platforms/) должны быть заданы ДО
    // конструктора QGuiApplication — иначе qwindows[d].dll не находится.
    const QString appDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    QCoreApplication::addLibraryPath(appDir);
    QCoreApplication::addLibraryPath(appDir + QStringLiteral("/plugins"));

    QGuiApplication app(argc, argv);

    auto resolveAppIcon = []() -> QIcon {
        // В retro-теме — свой значок (logo-retro.svg, тот же, что на сплэше),
        // а не иконка обычной темы из .exe/app.png. Экзешник на диске (тот,
        // что видно в Проводнике) при этом не перекрашивается — Windows
        // берёт его иконку из ресурса .exe статически на этапе сборки,
        // рантайм-подменой это не берётся без пересборки .ico; тут меняется
        // то, что реально видно, пока приложение открыто — заголовок окна,
        // Alt+Tab, taskbar.
        if (AppConfig::instance()->theme() == QLatin1String("retro")) {
            const QStringList retroCandidates = {
                QStringLiteral(":/qt/qml/AnimeClient/qml/assets/logo-retro.svg"),
                QStringLiteral(":/AnimeClient/qml/assets/logo-retro.svg"),
            };
            for (const QString &path : retroCandidates) {
                const QIcon icon(path);
                if (!icon.isNull())
                    return icon;
            }
        }
#ifdef Q_OS_WIN
        const QIcon fromExe(QCoreApplication::applicationFilePath());
        if (!fromExe.isNull())
            return fromExe;
#endif
        const QStringList candidates = {
            QStringLiteral(":/qt/qml/AnimeClient/qml/assets/app.png"),
            QStringLiteral(":/AnimeClient/qml/assets/app.png"),
        };
        for (const QString &path : candidates) {
            const QIcon icon(path);
            if (!icon.isNull())
                return icon;
        }
        return {};
    };

    const QIcon appIcon = resolveAppIcon();
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    // Qt выставляет locale в конструкторе; libmpv требует LC_NUMERIC=C.
    std::setlocale(LC_NUMERIC, "C");
    installFileLogger();
    installCrashTrace();
    qInfo("Anime Client started");

    qmlRegisterType<MpvPlayer>("AnimeClient", 1, 0, "MpvPlayer");
    // Алиас из mpv-examples (MpvObject → MpvVideo): тот же C++ класс, другое имя в QML.
    qmlRegisterType<MpvPlayer>("Mpv", 1, 0, "MpvVideo");
    qmlRegisterType<PlaybackController>("AnimeClient", 1, 0, "PlaybackController");
    qmlRegisterType<CatalogBridge>("AnimeClient", 1, 0, "CatalogBridge");
    qmlRegisterType<BookmarksBridge>("AnimeClient", 1, 0, "BookmarksBridge");
    qmlRegisterType<DetailBridge>("AnimeClient", 1, 0, "DetailBridge");
    qmlRegisterType<PosterThumbnail>("AnimeClient", 1, 0, "PosterThumbnail");

    // RetroTheme — QML-синглтон через C++, а не `pragma Singleton` в .qml.
    // Тот вариант давал гонку: RetroTheme.primary резолвился в undefined на
    // первом кадре, когда синглтон использовался в top-level биндингах
    // root-компонента, загружаемого через loadFromModule. qmlRegisterSingle-
    // tonInstance регистрирует готовый экземпляр синхронно, до первой
    // попытки engine его использовать — гонки нет в принципе.
    static RetroTheme retroThemeInstance;
    qmlRegisterSingletonInstance<RetroTheme>("AnimeClient", 1, 0, "RetroTheme", &retroThemeInstance);

    QQmlApplicationEngine engine;
    auto *posterProvider = new PosterImageProvider();
    posterProvider->setParent(&engine);
    engine.addImageProvider(QStringLiteral("posters"), posterProvider);
    // Embedded QML-модуль лежит в ресурсах под :/AnimeClient (qmldir +
    // qml/Main.qml) — без явного импорт-пути движок иногда не находит его
    // через loadFromModule (наблюдалось как "Module AnimeClient contains
    // no type named Main" при старте).
    engine.addImportPath(":/");
    // QtQuick.Controls и зависимые модули задеплоены рядом с exe (см.
    // CMakeLists.txt) как runtime QML-плагины, а не слинкованы статически —
    // qt_import_qml_plugins() оказался ненадёжен на чистой сборке.
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.rootContext()->setContextProperty("appConfig", AppConfig::instance());
    engine.rootContext()->setContextProperty("historyManager", HistoryManager::instance());
    engine.rootContext()->setContextProperty("posterCache", PosterCache::instance());

    // Theme — обычный QObject как контекстное свойство (не QML-синглтон),
    // ровно как в Python-версии (qt_bridge/theme.py) — старые компоненты
    // (Card.qml и т.п.) ссылаются на "Theme.xxx" как на глобальный объект.
    static Theme themeInstance;
    engine.rootContext()->setContextProperty("Theme", &themeInstance);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [&engine]() {
                         qCritical("QQmlApplicationEngine failed to load component");
                         const auto roots = engine.rootObjects();
                         Q_UNUSED(roots);
                         QCoreApplication::exit(-1);
                     },
                     Qt::QueuedConnection);

    QObject::connect(
        &engine, &QQmlApplicationEngine::warnings, &app,
        [](const QList<QQmlError> &warnings) {
            for (const QQmlError &e : warnings)
                qWarning("%s", qUtf8Printable(e.toString()));
        });

    // Выбор темы — читается один раз при старте (переключение в Настройках
    // требует перезапуска приложения, см. SettingsView.qml). "retro" грузит
    // MainRetro.qml (RetroShell) вместо обычного Main.qml — параллельный,
    // полностью независимый набор экранов с локальными данными (retroData.js),
    // без сетевых бриджей.
    const QString rootComponent = AppConfig::instance()->theme() == QLatin1String("retro")
        ? QStringLiteral("MainRetro")
        : QStringLiteral("Main");
    engine.loadFromModule("AnimeClient", rootComponent);

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--auto-browse")) {
            QTimer::singleShot(2000, &engine, [&engine]() {
                qInfo("auto-browse: switchTab(1)");
                for (QObject *obj : engine.rootObjects()) {
                    if (QMetaObject::invokeMethod(obj, "switchTab", Q_ARG(QVariant, 1)))
                        return;
                }
                qWarning("auto-browse: switchTab failed");
            });
            break;
        }
        if (arg == QLatin1String("--auto-calendar")) {
            QTimer::singleShot(2500, &engine, [&engine]() {
                qInfo("auto-calendar: openBrowseCalendar()");
                for (QObject *obj : engine.rootObjects()) {
                    if (QMetaObject::invokeMethod(obj, "openBrowseCalendar"))
                        return;
                }
                qWarning("auto-calendar: openBrowseCalendar failed");
            });
            break;
        }
    }

    const auto applyWindowIcon = [&appIcon](QObject *obj) {
        if (appIcon.isNull() || !obj)
            return;
        if (auto *window = qobject_cast<QQuickWindow *>(obj))
            window->setIcon(appIcon);
    };

    for (QObject *obj : engine.rootObjects())
        applyWindowIcon(obj);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, applyWindowIcon);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qInfo("Anime Client shutting down");
        // std::quick_exit ниже пропускает статические деструкторы, поэтому всё,
        // что синглтоны делали бы в деструкторах, выполняем явно здесь:
        // 1) прогресс просмотра (иначе теряются последние ≤5 сек тайминга);
        HistoryManager::instance()->flush();
        // 2) PosterThumbnail декодирует в QtConcurrent — дождёмся, чтобы не
        //    сыпались QWaitCondition / QThreadStorage в лог при выходе (Debug);
        QThreadPool::globalInstance()->waitForDone(3000);
        // 3) http-потоки QNetworkAccessManager (join в деструкторе QNAM).
        NetworkManager::instance()->shutdown();
    });

    const int code = app.exec();
    qInfo("Anime Client exit code: %d", code);

    // std::quick_exit: обычный return из main крашится (0xC0000005) в
    // статических деструкторах синглтонов (PosterCache/HistoryManager/… живут
    // как function-local statics и умирают ПОСЛЕ QGuiApplication — проверено
    // повторно 2026-07: даже с NetworkManager::shutdown() segfault остаётся).
    // Всё критичное (flush прогресса, join потоков) выполняется явно в
    // aboutToQuit выше; оставшиеся QWaitCondition/QThreadStorage-строки в логе
    // после "exit code" — косметика выгрузки Qt DLL, не влияет на данные.
    std::quick_exit(code < 0 ? 1 : code);
}
