#include "NetworkManager.h"

#include <QNetworkCookieJar>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>


namespace {

// localhost + Shikimori — без прокси (быстро и без обрывов, см. PosterCache-
// логи). kodikplayer.com — через прокси (геоблок региона без него); при 504
// KodikClient делает retry.
//
// AniList раньше тоже был тут («через socks5-прокси Kodik часто давал Host
// not found»), но это форсировало ВЕСЬ *.anilist.co (включая CDN-картинки
// s4.anilist.co) в обход прокси безусловно — а на практике для прокси этого
// пользователя прямое соединение к s4.anilist.co стабильно виснет (см.
// PosterCache::attachStallWatch — "no data for 30 s", повторяется при каждом
// перезапуске), тогда как через прокси работает. GraphQL-запросы к самому
// AniList API (AniListClient) используют postLocal() напрямую и это правило
// не затрагивает — баннер-CDN теперь идёт по общему правилу (прокси, если
// включён в настройках), как остальные внешние запросы.
bool preferDirectConnection(const QUrl &url) {
    const QString host = url.host().toLower();
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1"))
        return true;
    if (host.endsWith(QLatin1String(".local")))
        return true;
    if (host == QLatin1String("shikimori.io") || host == QLatin1String("shikimori.one"))
        return true;
    if (host.endsWith(QLatin1String(".astar.bz")))
        return true;
    if (host.contains(QLatin1String("hentasis")))
        return true;
    return false;
}

// Shikimori/AniList через Qt+HTTP/2 часто падают с "Connection closed" на Windows —
// принудительно HTTP/1.1 и таймаут, иначе каталог зависает пустым.
void tuneRequest(QNetworkRequest &req) {
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (req.attribute(NetworkManager::ImageDownloadRequest).toBool())
        return;
    if (req.transferTimeout() <= 0)
        req.setTransferTimeout(30000);
}

void installDiskCache(QNetworkAccessManager &nam) {
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/http");
    auto *diskCache = new QNetworkDiskCache(&nam);
    diskCache->setCacheDirectory(cacheDir);
    diskCache->setMaximumCacheSize(64 * 1024 * 1024);
    nam.setCache(diskCache);
}

}

NetworkManager *NetworkManager::instance() {
    static NetworkManager inst;
    return &inst;
}

NetworkManager::NetworkManager(QObject *parent) : QObject(parent) {
    QMetaObject::invokeMethod(this, [this]() {
        connect(AppConfig::instance(), &AppConfig::proxyChanged, this, &NetworkManager::refreshProxy);
        refreshProxy();
    }, Qt::QueuedConnection);
}

QNetworkAccessManager *NetworkManager::local() {
    if (!m_namLocal) {
        m_namLocal = std::make_unique<QNetworkAccessManager>(this);
        installDiskCache(*m_namLocal);
        m_namLocal->setProxy(QNetworkProxy::NoProxy);
    }
    return m_namLocal.get();
}

QNetworkAccessManager *NetworkManager::external() {
    if (!m_namExternal) {
        // Внешний NAM (прокси/Kodik) без disk cache — 504/502 не должны кэшироваться.
        m_namExternal = std::make_unique<QNetworkAccessManager>(this);
        m_namExternal->setCookieJar(new QNetworkCookieJar(m_namExternal.get()));
        refreshProxy();
    }
    return m_namExternal.get();
}

void NetworkManager::shutdown() {
    // ~QNetworkAccessManager join'ит свой http-поток.
    m_namExternal.reset();
    m_namLocal.reset();
}

void NetworkManager::refreshProxy() {
    if (!m_namExternal)
        return; // применится лениво при создании
    AppConfig *cfg = AppConfig::instance();
    if (!cfg->proxyEnabled() || cfg->proxyHost().isEmpty() || cfg->proxyPort() <= 0) {
        m_namExternal->setProxy(QNetworkProxy::NoProxy);
        installDiskCache(*m_namExternal);
        qInfo("NetworkManager: proxy off (config %s)", qUtf8Printable(cfg->settingsFilePath()));
        return;
    }
    m_namExternal->setCache(nullptr);
    QNetworkProxy proxy;
    proxy.setType(cfg->proxyType() == "socks5" ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy);
    proxy.setHostName(cfg->proxyHost());
    proxy.setPort(static_cast<quint16>(cfg->proxyPort()));
    if (!cfg->proxyUser().isEmpty()) {
        proxy.setUser(cfg->proxyUser());
        proxy.setPassword(cfg->proxyPassword());
    }
    m_namExternal->setProxy(proxy);
    qInfo(
        "NetworkManager: proxy on %s %s:%d user=%s (config %s)",
        qUtf8Printable(cfg->proxyType()),
        qUtf8Printable(cfg->proxyHost()),
        cfg->proxyPort(),
        cfg->proxyUser().isEmpty() ? "no" : "yes",
        qUtf8Printable(cfg->settingsFilePath()));
}

QNetworkReply *NetworkManager::get(const QNetworkRequest &request) {
    QNetworkRequest req(request);
    tuneRequest(req);
    auto *nam = preferDirectConnection(req.url()) ? local() : external();
    return nam->get(req);
}

QNetworkReply *NetworkManager::post(const QNetworkRequest &request, const QByteArray &body) {
    QNetworkRequest req(request);
    tuneRequest(req);
    auto *nam = preferDirectConnection(req.url()) ? local() : external();
    return nam->post(req, body);
}

QNetworkReply *NetworkManager::getLocal(const QNetworkRequest &request) {
    QNetworkRequest req(request);
    tuneRequest(req);
    return local()->get(req);
}

QNetworkReply *NetworkManager::postLocal(const QNetworkRequest &request, const QByteArray &body) {
    QNetworkRequest req(request);
    tuneRequest(req);
    return local()->post(req, body);
}
