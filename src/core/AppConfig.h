#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantMap>

// Общие настройки приложения (тема, громкость, пути, прокси).
// Прогресс просмотра сюда НЕ входит — для него HistoryManager + SQLite.
class AppConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mpvPath READ mpvPath WRITE setMpvPath NOTIFY mpvPathChanged)
    Q_PROPERTY(QString torrServerPath READ torrServerPath WRITE setTorrServerPath NOTIFY torrServerPathChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)

    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyType READ proxyType WRITE setProxyType NOTIFY proxyChanged) // "http" | "socks5"
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY proxyChanged)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyUser READ proxyUser WRITE setProxyUser NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyPassword READ proxyPassword WRITE setProxyPassword NOTIFY proxyChanged)

    Q_PROPERTY(bool excludeChinese READ excludeChinese WRITE setExcludeChinese NOTIFY excludeChineseChanged)
    Q_PROPERTY(QString jacredUrl READ jacredUrl WRITE setJacredUrl NOTIFY jacredUrlChanged)
    // Пусто — KodikClient сам берёт токен из публичного tokens.json.
    Q_PROPERTY(QString kodikToken READ kodikToken WRITE setKodikToken NOTIFY kodikTokenChanged)
    // Локальный sidecar (Playwright): http://127.0.0.1:8765 — обход DDoS-Guard для /ftor
    Q_PROPERTY(QString kodikResolverUrl READ kodikResolverUrl WRITE setKodikResolverUrl NOTIFY kodikResolverUrlChanged)
    // "auto" — GPU с fallback на CPU; "gpu" — только OpenGL; "software" — только CPU
    Q_PROPERTY(QString playerRenderMode READ playerRenderMode WRITE setPlayerRenderMode NOTIFY playerRenderModeChanged)
    // auto | unlimited | 120 | 60 | 30 — лимит перерисовки плеера (не декодера)
    Q_PROPERTY(QString playerFpsLimit READ playerFpsLimit WRITE setPlayerFpsLimit NOTIFY playerFpsLimitChanged)

public:
    explicit AppConfig(QObject *parent = nullptr);

    static AppConfig *instance();

    QString mpvPath() const;
    void setMpvPath(const QString &path);

    QString torrServerPath() const;
    void setTorrServerPath(const QString &path);

    int volume() const;
    void setVolume(int v);

    QString theme() const;
    void setTheme(const QString &t);

    bool proxyEnabled() const;
    void setProxyEnabled(bool on);

    QString proxyType() const;
    void setProxyType(const QString &type);

    QString proxyHost() const;
    void setProxyHost(const QString &host);

    int proxyPort() const;
    void setProxyPort(int port);

    QString proxyUser() const;
    void setProxyUser(const QString &user);

    QString proxyPassword() const;
    void setProxyPassword(const QString &password);

    bool excludeChinese() const;
    void setExcludeChinese(bool value);

    QString jacredUrl() const;
    void setJacredUrl(const QString &url);

    QString kodikResolverUrl() const;
    void setKodikResolverUrl(const QString &url);

    QString playerRenderMode() const;
    void setPlayerRenderMode(const QString &mode);

    QString playerFpsLimit() const;
    void setPlayerFpsLimit(const QString &limit);

    QString kodikToken() const;
    void setKodikToken(const QString &token);

    // "http://host:port" — для mpv --http-proxy / --all-proxy. Пустая строка,
    // если прокси выключен (для AniLibria/торрентов он не нужен).
    Q_INVOKABLE QString mpvProxyUrl() const;
    Q_INVOKABLE QString autoDetectTorrServer() const;

    // Общая папка config.ini + history.sqlite3 (вне песочницы Claude/AppData).
    static QString dataDirectory();
    // Путь к локальной БД истории просмотра (HistoryManager).
    static QString historyDbPath();

    QString settingsFilePath() const;

    // Снимок hero-баннера "Продолжить: ..." — чтобы при старте приложения
    // сразу показать правильную обложку из кэша, не дожидаясь сетевого
    // ответа Shikimori/AniList (иначе долю секунды виден старый/чужой кадр).
    Q_INVOKABLE void setLastHero(const QVariantMap &item);
    Q_INVOKABLE QVariantMap lastHero() const;
    Q_INVOKABLE void clearLastHero();

    // Последняя реально использованная связка источник+озвучка для режима
    // "Смэш" (авто-выбор лучшего), отдельно по каждому тайтлу.
    Q_INVOKABLE void setSmashChoice(const QString &titleId, const QString &translationId);
    Q_INVOKABLE QString smashChoice(const QString &titleId) const;

    // Пресет ручного сдвига звука относительно видео в смэше, по тайтлу.
    Q_INVOKABLE void setAudioSyncOffset(const QString &titleId, double seconds);
    Q_INVOKABLE double audioSyncOffset(const QString &titleId) const;

    // Смена темы (обычная/retro) читается в main.cpp только один раз при
    // старте — перезапуск процесса нужен, чтобы применить. Запускает новый
    // экземпляр exe и завершает текущий процесс.
    Q_INVOKABLE void restartApplication();

signals:
    void mpvPathChanged();
    void torrServerPathChanged();
    void volumeChanged();
    void themeChanged();
    void proxyChanged();
    void excludeChineseChanged();
    void jacredUrlChanged();
    void kodikTokenChanged();
    void kodikResolverUrlChanged();
    void playerRenderModeChanged();
    void playerFpsLimitChanged();

private:
    QSettings m_settings;
};
