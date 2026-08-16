#include "ThumbnailProbe.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <cstdio>
#include <cstring>

ThumbnailImageProvider::ThumbnailImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbnailImageProvider::requestImage(const QString &, QSize *size, const QSize &requestedSize) {
    QImage img = ThumbnailProbe::instance()->lastImage();
    if (img.isNull())
        return {};
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = img.size();
    return img;
}

namespace {

// Кадр из ответа screenshot-raw (mpv_node: w/h/stride/format/img).
QImage imageFromRawScreenshot(const mpv_node &node) {
    if (node.format != MPV_FORMAT_NODE_ARRAY)
        return {};
    const mpv_node_list *list = node.u.list;
    if (!list)
        return {};

    int w = 0, h = 0, stride = 0;
    QByteArray bytes;
    QImage::Format fmt = QImage::Format_Invalid;
    bool needSwap = false;

    for (int i = 0; i + 1 < list->num; i += 2) {
        const mpv_node &k = list->values[i];
        const mpv_node &v = list->values[i + 1];
        if (k.format != MPV_FORMAT_STRING)
            continue;
        const QByteArray key = QByteArray(k.u.string);
        if (key == "w" && v.format == MPV_FORMAT_INT64)
            w = static_cast<int>(v.u.int64);
        else if (key == "h" && v.format == MPV_FORMAT_INT64)
            h = static_cast<int>(v.u.int64);
        else if (key == "stride" && v.format == MPV_FORMAT_INT64)
            stride = static_cast<int>(v.u.int64);
        else if (key == "format" && v.format == MPV_FORMAT_STRING) {
            const QByteArray f = QByteArray(v.u.string);
            // Порядок байт в памяти: на little-endian "bgr0" = B,G,R,0 =
            // QImage::Format_RGB32 (0xffRRGGBB), "rgb0" — наоборот (swap).
            if (f == "bgr0")
                fmt = QImage::Format_RGB32;
            else if (f == "rgb0") {
                fmt = QImage::Format_RGB32;
                needSwap = true;
            } else if (f == "bgra")
                fmt = QImage::Format_ARGB32;
            else if (f == "rgba") {
                fmt = QImage::Format_RGBA8888;
            } else if (f == "rgb24")
                fmt = QImage::Format_RGB888;
            else if (f == "bgr24") {
                fmt = QImage::Format_RGB888;
                needSwap = true;
            }
        } else if (key == "img" && v.format == MPV_FORMAT_BYTE_ARRAY && v.u.ba) {
            bytes = QByteArray(static_cast<const char *>(v.u.ba->data),
                               static_cast<int>(v.u.ba->size));
        }
    }

    if (w <= 0 || h <= 0 || stride <= 0 || bytes.isEmpty() || fmt == QImage::Format_Invalid)
        return {};

    QImage img(w, h, fmt);
    if (img.bytesPerLine() == stride) {
        memcpy(img.bits(), bytes.constData(), static_cast<size_t>(h) * stride);
    } else {
        // Страйд может быть больше w*bpp (выравнивание) — копируем построчно.
        const int copy = qMin(img.bytesPerLine(), stride);
        for (int y = 0; y < h; ++y)
            memcpy(img.scanLine(y), bytes.constData() + static_cast<size_t>(y) * stride,
                   static_cast<size_t>(copy));
    }
    if (needSwap)
        img = img.rgbSwapped();
    return img;
}

} // namespace

ThumbnailProbe::ThumbnailProbe(QObject *parent) : QObject(parent) {
    m_mpv = mpv_create();
    if (!m_mpv)
        return;
    // Тихий режим: без вывода, без звука/субтитров, в паузе.
    mpv_set_option_string(m_mpv, "vo", "null");
    mpv_set_option_string(m_mpv, "aid", "no");
    mpv_set_option_string(m_mpv, "sid", "no");
    mpv_set_option_string(m_mpv, "pause", "yes");
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "osd-level", "0");
    // Малый кэш — превью на коротких seek'ах, полный файл не нужен.
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "8388608");   // 8 MiB
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "4194304");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "cache-secs", "2");
    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_set_wakeup_callback(m_mpv, wakeupCallback, this);
}

ThumbnailProbe::~ThumbnailProbe() {
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

ThumbnailProbe *ThumbnailProbe::instance() {
    static ThumbnailProbe *inst = new ThumbnailProbe(QCoreApplication::instance());
    return inst;
}

void ThumbnailProbe::wakeupCallback(void *ctx) {
    auto *self = static_cast<ThumbnailProbe *>(ctx);
    QMetaObject::invokeMethod(self, "onMpvEvents", Qt::QueuedConnection);
}

void ThumbnailProbe::onMpvEvents() {
    if (!m_mpv)
        return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        switch (event->event_id) {
        case MPV_EVENT_FILE_LOADED:
            m_fileReady = true;
            // Был запрос на кадр, пока файл грузился — выполняем его сейчас.
            if (m_pendingSeconds >= 0.0)
                doSeekAndShot();
            break;
        case MPV_EVENT_END_FILE:
            m_fileReady = false;
            m_lastImage = QImage();
            break;
        case MPV_EVENT_COMMAND_REPLY:
            handleCommandReply(event);
            break;
        default:
            break;
        }
    }
}

void ThumbnailProbe::handleCommandReply(mpv_event *event) {
    const uint64_t id = event->reply_userdata;
    if (id == kCmdLoad) {
        if (event->error < 0)
            qWarning("ThumbnailProbe: loadfile failed: %s", mpv_error_string(event->error));
        return;
    }
    if (id == kCmdSeek) {
        if (event->error < 0) {
            qWarning("ThumbnailProbe: seek failed: %s", mpv_error_string(event->error));
            m_shotInFlight = false;
            if (m_pendingSeconds >= 0.0)
                doSeekAndShot(); // пришёл новый запрос во время неудачного seek
            return;
        }
        sendScreenshotRequest();
        return;
    }
    if (id == kCmdShot) {
        m_shotInFlight = false;
        const double seconds = m_shotSeconds;
        m_shotSeconds = -1.0;
        if (event->error < 0) {
            qWarning("ThumbnailProbe: screenshot-raw failed: %s", mpv_error_string(event->error));
        } else if (auto *cmd = static_cast<mpv_event_command *>(event->data)) {
            const QImage image = imageFromRawScreenshot(cmd->result);
            if (!image.isNull())
                applyImage(image, seconds);
        }
        // Запрос, пришедший, пока был в полёте текущий seek+shot.
        if (m_pendingSeconds >= 0.0)
            doSeekAndShot();
        return;
    }
}

void ThumbnailProbe::applyImage(const QImage &image, double seconds) {
    m_lastImage = image;
    ++m_frameVersion;
    emit thumbnailUpdated(seconds);
    emit frameChanged();
}

void ThumbnailProbe::loadIfNeeded(const QString &url, const QString &referer, const QString &proxyUrl) {
    if (url.isEmpty())
        return;
    if (url == m_loadedUrl && m_fileReady)
        return;
    ensureLoaded(url, referer, proxyUrl);
}

void ThumbnailProbe::ensureLoaded(const QString &url, const QString &referer, const QString &proxyUrl) {
    if (!m_mpv)
        return;
    m_loadedUrl = url;
    m_fileReady = false;
    m_pendingSeconds = -1.0;
    m_shotInFlight = false;
    m_lastImage = QImage();
    m_frameVersion = 0;
    emit frameChanged();

    // Те же referrer/http-proxy, что у основного плеера (MpvPlayer::playUrl).
    if (referer.isEmpty())
        mpv_set_property_string(m_mpv, "referrer", "");
    else
        mpv_set_property_string(m_mpv, "referrer", referer.toUtf8().constData());

    mpv_set_property_string(
        m_mpv, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    mpv_set_property(m_mpv, "http-proxy", MPV_FORMAT_NONE, nullptr);
    mpv_set_property(m_mpv, "proxy", MPV_FORMAT_NONE, nullptr);
    if (!proxyUrl.isEmpty()) {
        const QByteArray proxyBytes = proxyUrl.toUtf8();
        if (proxyUrl.startsWith(QStringLiteral("socks5"), Qt::CaseInsensitive))
            mpv_set_property_string(m_mpv, "proxy", proxyBytes.constData());
        else
            mpv_set_property_string(m_mpv, "http-proxy", proxyBytes.constData());
    }

    // m_loadUrlBytes живёт до COMMAND_REPLY (mpv_command_async не копирует args).
    m_loadUrlBytes = url.toUtf8();
    const char *cmd[] = {"loadfile", m_loadUrlBytes.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, kCmdLoad, cmd);
}

void ThumbnailProbe::requestThumbnail(double seconds) {
    if (seconds < 0.0)
        return;
    m_pendingSeconds = seconds;
    if (!m_mpv || !m_fileReady)
        return; // дождёмся FILE_LOADED
    if (m_shotInFlight)
        return; // новый запрос учтён в m_pendingSeconds — выполнится после shot
    doSeekAndShot();
}

void ThumbnailProbe::doSeekAndShot() {
    if (!m_mpv)
        return;
    m_shotInFlight = true;
    m_shotSeconds = m_pendingSeconds;
    m_pendingSeconds = -1.0;
    // m_seekArg живёт до COMMAND_REPLY.
    m_seekArg = QByteArray::number(m_shotSeconds, 'f', 3);
    const char *cmd[] = {"seek", m_seekArg.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, kCmdSeek, cmd);
}

void ThumbnailProbe::sendScreenshotRequest() {
    if (!m_mpv)
        return;
    const char *cmd[] = {"screenshot-raw", nullptr};
    mpv_command_async(m_mpv, kCmdShot, cmd);
}

void ThumbnailProbe::stop() {
    m_fileReady = false;
    m_pendingSeconds = -1.0;
    m_shotInFlight = false;
    m_lastImage = QImage();
    m_frameVersion = 0;
    emit frameChanged();
    m_loadedUrl.clear();
    if (m_mpv)
        mpv_command_string(m_mpv, "stop");
}
