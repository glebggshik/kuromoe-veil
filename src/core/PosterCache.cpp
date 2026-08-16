#include "PosterCache.h"

#include "AppConfig.h"
#include "NetworkManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QElapsedTimer>
#include <QPointer>
#include <QStandardPaths>
#include <QDateTime>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <memory>

namespace {

bool isValidImageFile(const QString &path, const QString &remoteUrl);

} // namespace

PosterCache *PosterCache::instance() {
    static PosterCache *inst = new PosterCache(QCoreApplication::instance());
    return inst;
}

PosterCache::PosterCache(QObject *parent) : QObject(parent) {
    QTimer::singleShot(5000, this, &PosterCache::enforceCacheLimit);
}

QString PosterCache::cacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/posters");
}

QString PosterCache::cachePathFor(const QString &remoteUrl) const {
    // Единый jpeg-path: кэш ВСЕГДА хранит пере-кодированный JPEG
    // (writeValidatedImage сохраняет в JPEG), независимо от формата
    // исходника (.jpg/.png/.webp). Раньше расширение бралось из URL —
    // файл мог называться .webp, а содержать JPEG (или наоборот: webp-URL
    // вообще не сохранялся, decodeImageBytes требовал JPEG-маркеры).
    const QByteArray hash = QCryptographicHash::hash(remoteUrl.toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

QString PosterCache::posterId(const QString &remoteUrl) const {
    if (remoteUrl.isEmpty() || !isRemotePoster(remoteUrl))
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(remoteUrl.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool PosterCache::hasCached(const QString &remoteUrl) const {
    return !cachedFile(remoteUrl).isEmpty();
}

QString PosterCache::filePathForId(const QString &id) const {
    if (id.isEmpty())
        return {};
    const QString base = cacheDir() + QLatin1Char('/') + id;
    for (const QString &ext : {QStringLiteral("jpg"), QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("img")}) {
        const QString path = base + QLatin1Char('.') + ext;
        if (QFile::exists(path))
            return path;
    }
    return {};
}

bool PosterCache::validCached(const QString &path, const QString &remoteUrl) const {
    if (path.isEmpty())
        return false;
    const qint64 size = QFileInfo(path).size();
    if (size <= 0)
        return false;
    // Файл уже проходил полный декод и с тех пор не менялся (тот же размер) —
    // не декодируем повторно.
    auto it = m_validatedSizes.constFind(path);
    if (it != m_validatedSizes.constEnd() && it.value() == size)
        return true;
    if (isValidImageFile(path, remoteUrl)) {
        m_validatedSizes.insert(path, size);
        return true;
    }
    m_validatedSizes.remove(path);
    return false;
}

QString PosterCache::cachedFile(const QString &remoteUrl) const {
    if (remoteUrl.isEmpty())
        return {};
    if (!isRemotePoster(remoteUrl))
        return remoteUrl;

    if (m_done.contains(remoteUrl)) {
        const QString path = QUrl(m_done.value(remoteUrl)).toLocalFile();
        if (validCached(path, remoteUrl))
            return m_done.value(remoteUrl);
    }

    const QString path = cachePathFor(remoteUrl);
    if (validCached(path, remoteUrl))
        return QUrl::fromLocalFile(path).toString();
    return {};
}

bool PosterCache::isRemotePoster(const QString &url) const {
    return url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

int PosterCache::clearDiskCache() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    int removed = 0;
    for (const QString &sub : {QStringLiteral("posters"), QStringLiteral("http")}) {
        QDir dir(base + QLatin1Char('/') + sub);
        if (!dir.exists())
            continue;
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            if (QFile::remove(fi.absoluteFilePath()))
                ++removed;
        }
    }
    // Сбрасываем in-memory карту готовых, иначе cachedFile() вернёт пути на
    // только что удалённые файлы. Снимок hero в config тоже инвалидируем —
    // он ссылается на heroImageLocal, которого больше нет.
    m_done.clear();
    m_validatedSizes.clear();
    AppConfig::instance()->clearLastHero();
    qInfo("PosterCache: disk cache cleared (%d files)", removed);
    return removed;
}

void PosterCache::request(const QString &remoteUrl) {
    if (remoteUrl.isEmpty() || !isRemotePoster(remoteUrl))
        return;

    const QString existing = cachedFile(remoteUrl);
    if (!existing.isEmpty()) {
        m_done.insert(remoteUrl, existing);
        QMetaObject::invokeMethod(
            this, [this, remoteUrl, existing]() { emit posterReady(remoteUrl, existing); },
            Qt::QueuedConnection);
        return;
    }

    if (m_inFlight.contains(remoteUrl) || m_pendingHigh.contains(remoteUrl)
        || m_pendingNormal.contains(remoteUrl))
        return;

    m_pendingNormal.append(remoteUrl);
    pump();
}

void PosterCache::preloadCatalog(const QVariantList &items) {
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        const QString hd = item.value(QStringLiteral("posterHd")).toString();
        const QString poster = item.value(QStringLiteral("poster")).toString();
        auto usable = [](const QString &u) {
            return !u.isEmpty() && !u.contains(QStringLiteral("/missing_"));
        };
        if (usable(hd))
            request(hd);
        else if (usable(poster))
            request(poster);
    }
}

namespace {

constexpr int kStallTimeoutMs = 30000;
constexpr int kStallCheckIntervalMs = 1000;

void applyImageRequestHeaders(QNetworkRequest &req, const QString &url) {
    req.setRawHeader("Accept", "image/*,*/*");
    if (url.contains(QStringLiteral("anilist"), Qt::CaseInsensitive))
        req.setRawHeader("Referer", "https://anilist.co/");
    else
        req.setRawHeader("Referer", "https://shikimori.io/");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(NetworkManager::ImageDownloadRequest, true);
    req.setTransferTimeout(0);
}

struct DownloadProgress {
    qint64 lastBytes = 0;
    qint64 lastProgressMs = 0;
};

void noteDownloadProgress(DownloadProgress *state, qint64 bytes, qint64 elapsedMs) {
    if (bytes > state->lastBytes) {
        state->lastBytes = bytes;
        state->lastProgressMs = elapsedMs;
    } else if (state->lastProgressMs == 0) {
        state->lastProgressMs = elapsedMs;
    }
}

bool isDownloadStalled(const DownloadProgress &state, qint64 elapsedMs) {
    // До первого байта не рвём — только медленный канал, не обрыв.
    if (state.lastBytes <= 0)
        return false;
    if (state.lastProgressMs == 0)
        return false;
    return elapsedMs - state.lastProgressMs >= kStallTimeoutMs;
}

void logDownloadSpeed(const QString &url, qint64 bytes, qint64 elapsedMs) {
    if (bytes <= 0 || elapsedMs <= 0)
        return;
    const double sec = elapsedMs / 1000.0;
    const double kbPerSec = (bytes / 1024.0) / sec;
    qInfo(
        "PosterCache: download ok %s — %lld bytes, %.1f s, %.1f KB/s",
        qUtf8Printable(url),
        static_cast<long long>(bytes),
        sec,
        kbPerSec);
}

QByteArray readCompleteReplyData(QNetworkReply *reply, const QString &url) {
    QByteArray data = reply->readAll();
    const QVariant lenHdr = reply->header(QNetworkRequest::ContentLengthHeader);
    if (lenHdr.isValid()) {
        const qint64 expected = lenHdr.toLongLong();
        if (expected > 0 && data.size() < expected) {
            qWarning(
                "PosterCache: download truncated %s (%lld/%lld bytes)",
                qUtf8Printable(url),
                static_cast<long long>(data.size()),
                static_cast<long long>(expected));
            data.clear();
        }
    }
    return data;
}

void attachStallWatch(PosterCache *owner, QNetworkReply *reply, const QString &url) {
    auto progress = std::make_shared<DownloadProgress>();
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    auto *stallCheck = new QTimer(owner);
    stallCheck->setInterval(kStallCheckIntervalMs);

    QObject::connect(stallCheck, &QTimer::timeout, owner, [reply, progress, timer, url, stallCheck]() {
        if (reply->isFinished()) {
            stallCheck->stop();
            return;
        }
        const qint64 elapsed = timer->elapsed();
        if (isDownloadStalled(*progress, elapsed)) {
            qWarning(
                "PosterCache: download stalled %s (no data for %d s)",
                qUtf8Printable(url),
                kStallTimeoutMs / 1000);
            stallCheck->stop();
            reply->abort();
        }
    });
    QObject::connect(
        reply, &QNetworkReply::downloadProgress, reply,
        [progress, timer](qint64 received, qint64) {
            noteDownloadProgress(progress.get(), received, timer->elapsed());
        });
    QObject::connect(reply, &QNetworkReply::finished, stallCheck, [stallCheck]() {
        stallCheck->stop();
        stallCheck->deleteLater();
    });
    stallCheck->start();
}

bool directImageUrl(const QString &url) {
    // Раньше AniList тоже форсировался напрямую ("через socks5-прокси Kodik
    // часто даёт Host not found"), но это оказалось верно не для всех прокси:
    // по логам прямое соединение к s4.anilist.co стабильно виснет ("download
    // stalled ... no data for 30 s", повторяется при каждом перезапуске), а
    // через прокси работает нормально. Shikimori, наоборот, напрямую качается
    // быстро и без обрывов — его и оставляем на прямом соединении. AniList
    // теперь идёт по общему правилу (через прокси, если включён в настройках).
    return url.contains(QStringLiteral("shikimori"), Qt::CaseInsensitive);
}

QNetworkReply *startImageDownload(const QNetworkRequest &baseReq, const QString &url) {
    if (directImageUrl(url))
        return NetworkManager::instance()->getLocal(baseReq);
    if (AppConfig::instance()->proxyEnabled())
        return NetworkManager::instance()->get(baseReq);
    return NetworkManager::instance()->getLocal(baseReq);
}

const char *imageDownloadRoute(const QString &url) {
    if (url.contains(QStringLiteral("shikimori"), Qt::CaseInsensitive))
        return "direct (shikimori)";
    if (AppConfig::instance()->proxyEnabled())
        return "via proxy";
    return "direct";
}

bool expectWideBanner(const QString &remoteUrl) {
    return remoteUrl.contains(QStringLiteral("anilist"), Qt::CaseInsensitive)
        || remoteUrl.contains(QStringLiteral("banner"), Qt::CaseInsensitive);
}

bool isJpegData(const QByteArray &data) {
    return data.size() >= 2
        && static_cast<unsigned char>(data[0]) == 0xFF
        && static_cast<unsigned char>(data[1]) == 0xD8;
}

bool hasJpegEndMarker(const QByteArray &data) {
    if (!isJpegData(data))
        return true;
    for (int i = data.size() - 2; i >= qMax(0, data.size() - 4096); --i) {
        if (static_cast<unsigned char>(data[i]) == 0xFF
            && static_cast<unsigned char>(data[i + 1]) == 0xD9)
            return true;
    }
    return false;
}

bool hasJpegEndMarkerFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return hasJpegEndMarker(file.readAll());
}

// Проверка по одним габаритам (без пикселей) — общая для файла и QImage.
bool isValidImageDims(int w, int h, const QString &remoteUrl) {
    if (w < 200 || h < 80)
        return false;
    if (expectWideBanner(remoteUrl)) {
        if (w < 500 || w < h * 12 / 10)
            return false;
    }
    return true;
}

bool isValidHeroImage(const QImage &img, const QString &remoteUrl) {
    if (img.isNull())
        return false;
    return isValidImageDims(img.width(), img.height(), remoteUrl);
}

void ensureSrgb8(QImage *img) {
    if (!img || img->isNull())
        return;
    const QColorSpace srgb{QColorSpace::SRgb};
    if (img->colorSpace().isValid() && img->colorSpace() != srgb)
        *img = img->convertedToColorSpace(srgb);
    else
        img->setColorSpace(srgb);
    // 16-bit / float на 8-bit Wayland без управления цветом даёт «мало цветов».
    switch (img->format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGB888:
        break;
    default:
        *img = img->convertToFormat(QImage::Format_RGB888);
        img->setColorSpace(srgb);
        break;
    }
}

// PNG/WebP магические байты — JPEG-проверка по концевым маркерам их не
// пропускает, а такие постеры (AniList/Shikimori и др.) реально приходят.
bool isPngOrWebp(const QByteArray &data) {
    if (data.size() >= 12 && data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
        return true;
    if (data.size() >= 8 && static_cast<unsigned char>(data.at(0)) == 0x89
        && data.mid(1, 3) == "PNG")
        return true;
    return false;
}

bool decodeImageBytes(const QByteArray &data, const QString &remoteUrl, QImage *out) {
    if (data.size() < 1024)
        return false;
    // JPEG — быстрое отсечение по концевым маркерам (ловит обрезанные
    // загрузки); PNG/WebP проверяем по магическим байтам. WebP декодируется
    // только при наличии плагина (qtimageformats) — иначе QImageReader вернёт
    // null, и файл честно отбракуется, а не молча сохранится битым.
    if (!hasJpegEndMarker(data) && !isPngOrWebp(data))
        return false;
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull() || !isValidHeroImage(img, remoteUrl))
        return false;
    ensureSrgb8(&img);
    if (out)
        *out = img;
    return true;
}

bool isValidImageFile(const QString &path, const QString &remoteUrl) {
    if (path.isEmpty() || !QFile::exists(path))
        return false;
    const qint64 size = QFileInfo(path).size();
    if (size < 1024)
        return false;
    if (expectWideBanner(remoteUrl) && size < 30000)
        return false;
    if (!hasJpegEndMarkerFile(path))
        return false;
    // Габариты берём из заголовка (QImageReader::size) без полного декода
    // пикселей — раньше здесь был reader.read() на весь JPEG, и это была
    // основная стоимость синхронной валидации при первом заходе на вкладку.
    // Битость файла уже отсеяна проверкой JPEG-маркеров выше; окончательный
    // декод всё равно происходит в пуле при отрисовке (loadScaledImage).
    QImageReader reader(path);
    const QSize dim = reader.size();
    if (!dim.isValid())
        return false;
    return isValidImageDims(dim.width(), dim.height(), remoteUrl);
}

bool writeValidatedImage(const QString &dest, const QByteArray &data, const QString &remoteUrl) {
    QImage probe;
    if (!decodeImageBytes(data, remoteUrl, &probe))
        return false;
    QFile::remove(dest);
    // sRGB 8-bit JPEG: на Linux Qt/Wayland часто не применяет ICC из оригинала
    // (на Windows применяет) — фото выглядят выцветшими / «мало цветов».
    if (!probe.save(dest, "JPEG", 95))
        return false;
    return isValidImageFile(dest, remoteUrl);
}

} // namespace

void PosterCache::requestPriority(const QString &remoteUrl) {
    if (remoteUrl.isEmpty() || !isRemotePoster(remoteUrl))
        return;

    const QString existing = cachedFile(remoteUrl);
    if (!existing.isEmpty()) {
        m_done.insert(remoteUrl, existing);
        QMetaObject::invokeMethod(
            this, [this, remoteUrl, existing]() { emit posterReady(remoteUrl, existing); },
            Qt::QueuedConnection);
        return;
    }

    if (m_inFlight.contains(remoteUrl)) {
        m_pendingHigh.removeAll(remoteUrl);
        m_pendingNormal.removeAll(remoteUrl);
        m_pendingHigh.prepend(remoteUrl);
        return;
    }

    m_pendingNormal.removeAll(remoteUrl);
    if (!m_pendingHigh.contains(remoteUrl))
        m_pendingHigh.prepend(remoteUrl);
    pump();
}

void PosterCache::pump() {
    while (m_active < kMaxConcurrent) {
        QString url;
        if (!m_pendingHigh.isEmpty())
            url = m_pendingHigh.takeFirst();
        else if (!m_pendingNormal.isEmpty())
            url = m_pendingNormal.takeFirst();
        else
            break;
        if (m_inFlight.contains(url))
            continue;

        m_inFlight.insert(url);
        ++m_active;

        QDir().mkpath(cacheDir());
        const QString dest = cachePathFor(url);

        QNetworkRequest req{QUrl(url)};
        applyImageRequestHeaders(req, url);
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

        QNetworkReply *reply = startImageDownload(req, url);
        auto startedAt = std::make_shared<QElapsedTimer>();
        startedAt->start();
        attachStallWatch(this, reply, url);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, dest, startedAt]() {
            bool netOk = false;
            QByteArray data;
            if (reply->error() == QNetworkReply::NoError) {
                data = readCompleteReplyData(reply, url);
                if (!data.isEmpty()) {
                    logDownloadSpeed(url, data.size(), startedAt->elapsed());
                    netOk = true;
                }
            } else if (reply->error() != QNetworkReply::OperationCanceledError) {
                qWarning(
                    "PosterCache: download failed %s — %s",
                    qUtf8Printable(url),
                    qUtf8Printable(reply->errorString()));
            }
            reply->deleteLater();

            if (!netOk) {
                --m_active;
                m_inFlight.remove(url);
                pump();
                return;
            }

            // Декодирование + пере-сжатие JPEG (writeValidatedImage: decode →
            // encode q92 → контрольный decode) — десятки-сотни мс на картинку.
            // Раньше выполнялось прямо здесь, в UI-потоке: при загрузке
            // каталога (50 постеров) главная заметно фризилась. Уносим в пул.
            QPointer<PosterCache> self(this);
            (void)QtConcurrent::run([self, url, dest, data]() {
                const bool ok = writeValidatedImage(dest, data, url);
                if (!ok)
                    QFile::remove(dest);
                if (!self)
                    return;
                QMetaObject::invokeMethod(self.data(), [self, url, dest, ok]() {
                    if (!self)
                        return;
                    --self->m_active;
                    self->m_inFlight.remove(url);
                    if (ok) {
                        const QString fileUrl = QUrl::fromLocalFile(dest).toString();
                        self->m_done.insert(url, fileUrl);
                        emit self->posterReady(url, fileUrl);
                        self->scheduleCacheLimitCheck();
                    }
                    self->pump();
                }, Qt::QueuedConnection);
            });
        });
    }
}

void PosterCache::scheduleCacheLimitCheck() {
    if (m_lruPending)
        return;
    m_lruPending = true;
    QTimer::singleShot(2000, this, [this]() {
        m_lruPending = false;
        enforceCacheLimit();
    });
}

void PosterCache::enforceCacheLimit() {
    const QString dir = cacheDir();
    QDir qdir(dir);
    if (!qdir.exists())
        return;

    struct CacheEntry {
        QString path;
        qint64 size;
        QDateTime mtime;
    };

    QList<CacheEntry> entries;
    qint64 totalSize = 0;
    const auto files = qdir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    entries.reserve(files.size());
    for (const QFileInfo &fi : files) {
        entries.append(CacheEntry{fi.absoluteFilePath(), fi.size(), fi.lastModified()});
        totalSize += fi.size();
    }
    if (totalSize <= kMaxCacheSizeBytes)
        return;

    std::sort(entries.begin(), entries.end(), [](const CacheEntry &a, const CacheEntry &b) {
        return a.mtime < b.mtime;
    });

    int removed = 0;
    qint64 freed = 0;
    for (const auto &entry : entries) {
        if (totalSize <= kMaxCacheSizeBytes)
            break;
        if (!QFile::remove(entry.path))
            continue;
        totalSize -= entry.size;
        freed += entry.size;
        ++removed;
        m_validatedSizes.remove(entry.path);
        for (auto it = m_done.begin(); it != m_done.end();) {
            if (QUrl(it.value()).toLocalFile() == entry.path)
                it = m_done.erase(it);
            else
                ++it;
        }
    }

    if (removed > 0) {
        qInfo(
            "PosterCache: LRU cleanup removed %d files (%.1f MB freed, total %.1f MB)",
            removed,
            freed / (1024.0 * 1024.0),
            totalSize / (1024.0 * 1024.0));
    }
}