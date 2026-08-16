#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {

// Публичный токен Kodik (тот же, что в tools/) — не личный ключ, регистрации не требует.
// Если в config.ini пусто, клиент использует его; свой можно прописать в настройках.
const QString kDefaultKodikToken = QStringLiteral("56a768d08f43091901c44b54fe970049");

// QSettings иногда сохраняет логин/пароль с префиксом "\n" (буквальный \\n) — SOCKS5 auth падает.
QString sanitizeProxyCredential(const QString &value) {
    QString v = value;
    while (v.startsWith(QStringLiteral("\\n")))
        v = v.mid(2);
    while (!v.isEmpty() && (v.at(0) == QLatin1Char('\n') || v.at(0) == QLatin1Char('\r')))
        v = v.mid(1);
    return v.trimmed();
}

void repairProxyCredentials(QSettings &settings) {
    const QString user = settings.value(QStringLiteral("proxy/user")).toString();
    const QString pass = settings.value(QStringLiteral("proxy/password")).toString();
    const QString cleanUser = sanitizeProxyCredential(user);
    const QString cleanPass = sanitizeProxyCredential(pass);
    if (cleanUser == user && cleanPass == pass)
        return;
    settings.setValue(QStringLiteral("proxy/user"), cleanUser);
    settings.setValue(QStringLiteral("proxy/password"), cleanPass);
    settings.sync();
    qWarning("AppConfig: repaired proxy credentials (stripped \\n prefix)");
}

void migrateLegacyDataFile(const QString &targetDir, const QString &legacyDir, const QString &fileName) {
    const QString dst = QDir(targetDir).filePath(fileName);
    if (QFile::exists(dst))
        return;
    const QString src = QDir(legacyDir).filePath(fileName);
    if (!QFile::exists(src))
        return;
    if (QFile::copy(src, dst))
        qInfo("AppConfig: migrated %s from legacy %s", qUtf8Printable(fileName), qUtf8Printable(legacyDir));
}

QString &dataDirectoryCache() {
    static QString cached;
    return cached;
}

QString resolveDataDirectory() {
    QString &cached = dataDirectoryCache();
    if (!cached.isEmpty())
        return cached;

    const QByteArray env = qgetenv("ANIME_CLIENT_DATA_DIR");
    if (!env.isEmpty()) {
        cached = QDir::fromNativeSeparators(QString::fromUtf8(env).trimmed());
        QDir().mkpath(cached);
        return cached;
    }

    // Portable: config.ini рядом с exe (сборка для друга) — всё в каталоге exe.
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(QDir(exeDir).filePath(QStringLiteral("config.ini")))) {
        cached = exeDir;
        return cached;
    }

    // Общая папка в Documents — один профиль для обычного запуска и Claude Desktop.
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    cached = QDir(docs).filePath(QStringLiteral("AnimeClientData"));
    QDir().mkpath(cached);

    const QString legacy = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!legacy.isEmpty() && QDir(legacy).absolutePath() != QDir(cached).absolutePath()) {
        migrateLegacyDataFile(cached, legacy, QStringLiteral("config.ini"));
        migrateLegacyDataFile(cached, legacy, QStringLiteral("history.sqlite3"));
    }

    return cached;
}

QString configIniPath() {
    return QDir(resolveDataDirectory()).filePath(QStringLiteral("config.ini"));
}

} // namespace

AppConfig::AppConfig(QObject *parent)
    : QObject(parent),
      m_settings(configIniPath(), QSettings::IniFormat) {
    repairProxyCredentials(m_settings);
    qInfo("AppConfig: data dir %s", qUtf8Printable(dataDirectory()));
    qInfo("AppConfig: loaded %s", qUtf8Printable(m_settings.fileName()));
}

AppConfig *AppConfig::instance() {
    // Куча + parent=qApp: не умирает после QGuiApplication (иначе 0xC0000005).
    static AppConfig *inst = new AppConfig(QCoreApplication::instance());
    return inst;
}

QString AppConfig::settingsFilePath() const {
    return m_settings.fileName();
}

QString AppConfig::mpvPath() const { return m_settings.value("player/mpvPath").toString(); }
void AppConfig::setMpvPath(const QString &path) {
    if (path == mpvPath()) return;
    m_settings.setValue("player/mpvPath", path);
    emit mpvPathChanged();
}

QString AppConfig::torrServerPath() const { return m_settings.value("torrent/serverPath").toString(); }
void AppConfig::setTorrServerPath(const QString &path) {
    if (path == torrServerPath()) return;
    m_settings.setValue("torrent/serverPath", path);
    emit torrServerPathChanged();
}

QString AppConfig::torrServerHost() const {
    const QString host = m_settings.value("torrent/host", "127.0.0.1").toString().trimmed();
    return host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
}
void AppConfig::setTorrServerHost(const QString &host) {
    const QString clean = host.trimmed();
    if (clean == torrServerHost()) return;
    m_settings.setValue("torrent/host", clean);
    emit torrServerHostChanged();
}

int AppConfig::torrServerPort() const {
    const int port = m_settings.value("torrent/port", 8090).toInt();
    return (port > 0 && port <= 65535) ? port : 8090;
}
void AppConfig::setTorrServerPort(int port) {
    const int clamped = qBound(1, port, 65535);
    if (clamped == torrServerPort()) return;
    m_settings.setValue("torrent/port", clamped);
    emit torrServerPortChanged();
}

int AppConfig::volume() const { return m_settings.value("player/volume", 80).toInt(); }
void AppConfig::setVolume(int v) {
    v = qBound(0, v, 100);
    if (v == volume()) return;
    m_settings.setValue("player/volume", v);
    emit volumeChanged();
}

QString AppConfig::theme() const { return m_settings.value("ui/theme", "dark").toString(); }
void AppConfig::setTheme(const QString &t) {
    if (t == theme()) return;
    m_settings.setValue("ui/theme", t);
    emit themeChanged();
}

bool AppConfig::proxyEnabled() const {
    if (m_settings.contains(QStringLiteral("proxy/enabled")))
        return m_settings.value(QStringLiteral("proxy/enabled"), false).toBool();
    // Хост/порт уже заданы (миграция с Python-клиента) — считаем прокси включённым.
    return !proxyHost().isEmpty() && proxyPort() > 0;
}
void AppConfig::setProxyEnabled(bool on) {
    if (on == proxyEnabled()) return;
    m_settings.setValue("proxy/enabled", on);
    emit proxyChanged();
}

QString AppConfig::proxyType() const { return m_settings.value("proxy/type", "http").toString(); }
void AppConfig::setProxyType(const QString &type) {
    if (type == proxyType()) return;
    m_settings.setValue("proxy/type", type);
    emit proxyChanged();
}

QString AppConfig::proxyHost() const { return m_settings.value("proxy/host").toString(); }
void AppConfig::setProxyHost(const QString &host) {
    if (host == proxyHost()) return;
    m_settings.setValue("proxy/host", host);
    emit proxyChanged();
}

int AppConfig::proxyPort() const { return m_settings.value("proxy/port", 0).toInt(); }
void AppConfig::setProxyPort(int port) {
    if (port == proxyPort()) return;
    m_settings.setValue("proxy/port", port);
    emit proxyChanged();
}

QString AppConfig::proxyUser() const {
    return sanitizeProxyCredential(m_settings.value(QStringLiteral("proxy/user")).toString());
}
void AppConfig::setProxyUser(const QString &user) {
    const QString clean = sanitizeProxyCredential(user);
    if (clean == proxyUser()) return;
    m_settings.setValue(QStringLiteral("proxy/user"), clean);
    emit proxyChanged();
}

QString AppConfig::proxyPassword() const {
    return sanitizeProxyCredential(m_settings.value(QStringLiteral("proxy/password")).toString());
}
void AppConfig::setProxyPassword(const QString &password) {
    const QString clean = sanitizeProxyCredential(password);
    if (clean == proxyPassword()) return;
    m_settings.setValue(QStringLiteral("proxy/password"), clean);
    emit proxyChanged();
}

bool AppConfig::excludeChinese() const { return m_settings.value("catalog/excludeChinese", false).toBool(); }
void AppConfig::setExcludeChinese(bool value) {
    if (value == excludeChinese()) return;
    m_settings.setValue("catalog/excludeChinese", value);
    emit excludeChineseChanged();
}

QString AppConfig::jacredUrl() const { return m_settings.value("catalog/jacredUrl").toString(); }
void AppConfig::setJacredUrl(const QString &url) {
    QString trimmed = url.trimmed();
    while (trimmed.endsWith('/'))
        trimmed.chop(1);
    if (trimmed == jacredUrl()) return;
    m_settings.setValue("catalog/jacredUrl", trimmed);
    emit jacredUrlChanged();
}

QString AppConfig::playerRenderMode() const {
    const QString mode = m_settings.value("player/renderMode", "auto").toString();
    if (mode == QLatin1String("gpu") || mode == QLatin1String("software"))
        return mode;
    return QStringLiteral("auto");
}

void AppConfig::setPlayerRenderMode(const QString &mode) {
    QString normalized = mode;
    if (normalized != QLatin1String("gpu") && normalized != QLatin1String("software"))
        normalized = QStringLiteral("auto");
    if (normalized == playerRenderMode())
        return;
    m_settings.setValue("player/renderMode", normalized);
    emit playerRenderModeChanged();
}

QString AppConfig::playerFpsLimit() const {
    const QString limit = m_settings.value("player/fpsLimit", "auto").toString();
    if (limit == QLatin1String("unlimited") || limit == QLatin1String("120")
        || limit == QLatin1String("60") || limit == QLatin1String("30"))
        return limit;
    return QStringLiteral("auto");
}

void AppConfig::setPlayerFpsLimit(const QString &limit) {
    QString normalized = limit;
    if (normalized != QLatin1String("unlimited") && normalized != QLatin1String("120")
        && normalized != QLatin1String("60") && normalized != QLatin1String("30"))
        normalized = QStringLiteral("auto");
    if (normalized == playerFpsLimit())
        return;
    m_settings.setValue("player/fpsLimit", normalized);
    emit playerFpsLimitChanged();
}

QString AppConfig::kodikResolverUrl() const {
    return m_settings.value(QStringLiteral("kodik/resolverUrl")).toString().trimmed();
}

void AppConfig::setKodikResolverUrl(const QString &url) {
    const QString clean = url.trimmed();
    if (clean == kodikResolverUrl())
        return;
    m_settings.setValue(QStringLiteral("kodik/resolverUrl"), clean);
    emit kodikResolverUrlChanged();
}

QString AppConfig::kodikToken() const {
    const QString stored = m_settings.value("kodik/token").toString().trimmed();
    return stored.isEmpty() ? kDefaultKodikToken : stored;
}
void AppConfig::setKodikToken(const QString &token) {
    const QString clean = token.trimmed();
    if (clean == kodikToken())
        return;
    m_settings.setValue("kodik/token", clean);
    emit kodikTokenChanged();
}

void AppConfig::setLastHero(const QVariantMap &item) {
    const QJsonDocument doc(QJsonObject::fromVariantMap(item));
    m_settings.setValue(QStringLiteral("cache/lastHero"), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

QVariantMap AppConfig::lastHero() const {
    const QString raw = m_settings.value(QStringLiteral("cache/lastHero")).toString();
    if (raw.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    return doc.isObject() ? doc.object().toVariantMap() : QVariantMap();
}

void AppConfig::clearLastHero() {
    m_settings.remove(QStringLiteral("cache/lastHero"));
    m_settings.sync();
}

// Смэш ("авто-выбор лучшего") сам пересчитывает связку источник+озвучка
// каждый раз заново, из-за чего при повторном заходе на тайтл может
// выбрать другую озвучку, чем в прошлый раз. Запоминаем реально
// использованный translationId по titleId, чтобы смэш предпочитал именно
// его, если он всё ещё доступен.
void AppConfig::setSmashChoice(const QString &titleId, const QString &translationId) {
    if (titleId.isEmpty())
        return;
    m_settings.setValue(QStringLiteral("smash/%1").arg(titleId), translationId);
}

QString AppConfig::smashChoice(const QString &titleId) const {
    return m_settings.value(QStringLiteral("smash/%1").arg(titleId)).toString();
}

// Ручной сдвиг звук/видео в смэше (видео с торрента, звук с Kodik/CVH — у
// релизов разная длина опенинга/интро) — пользователь подбирает офсет один
// раз и сохраняет, чтобы не подбирать заново на каждой серии/при перезаходе.
void AppConfig::setAudioSyncOffset(const QString &titleId, double seconds) {
    if (titleId.isEmpty())
        return;
    m_settings.setValue(QStringLiteral("audioSync/%1").arg(titleId), seconds);
}

double AppConfig::audioSyncOffset(const QString &titleId) const {
    return m_settings.value(QStringLiteral("audioSync/%1").arg(titleId), 0.0).toDouble();
}

QString AppConfig::mpvProxyUrl() const {
    if (!proxyEnabled() || proxyHost().isEmpty() || proxyPort() <= 0)
        return QString();
    QString scheme = proxyType() == "socks5" ? "socks5" : "http";
    QString auth;
    if (!proxyUser().isEmpty())
        auth = proxyUser() + (proxyPassword().isEmpty() ? "" : ":" + proxyPassword()) + "@";
    return QString("%1://%2%3:%4").arg(scheme, auth, proxyHost()).arg(proxyPort());
}

QString AppConfig::autoDetectTorrServer() const {
    const QStringList names = {"TorrServer-windows-amd64.exe", "torrserver.exe"};
    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QDir::homePath() + "/Downloads",
    };
    for (const QString &dir : dirs) {
        if (dir.isEmpty())
            continue;
        for (const QString &name : names) {
            QString path = QDir(dir).filePath(name);
            if (QFile::exists(path))
                return path;
        }
    }
    return QString();
}

QString AppConfig::dataDirectory() {
    return resolveDataDirectory();
}

QString AppConfig::historyDbPath() {
    return QDir(dataDirectory()).filePath(QStringLiteral("history.sqlite3"));
}

void AppConfig::restartApplication() {
    // QSettings пишет на диск лениво (таймер/деструктор) — без явного sync()
    // новый процесс, запущенный сразу следом, мог прочитать ещё старый
    // config.ini (гонка, из-за которой смена темы не применялась).
    m_settings.sync();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    QCoreApplication::quit();
}
