#include "MpvPlayer.h"

#include "AppConfig.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <mpv/render_gl.h>
#include <QQuickWindow>

#include <cmath>
#include <cstring>

#include <QVariantMap>
#include <QUrl>
#include <QVector>

#if defined(Q_OS_LINUX)
#include <malloc.h>
#endif

namespace {
constexpr int kSwRedrawIntervalMs = 33;     // ~30 fps — только для CPU-пути
constexpr int kPositionEmitIntervalMs = 250;
constexpr double kPositionEmitDelta = 0.2;
constexpr int kMaxSwRenderPixels = 1280 * 720;

void *mpvGlGetProcAddress(void *ctx, const char *name) {
    Q_UNUSED(ctx);
    auto *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(name));
}

// Колбэки приходят из внутренних потоков mpv. Никогда не трогаем item без
// мьютекса — он может быть уже уничтожен (см. комментарий к MpvSharedState).
void wakeupCallback(void *ctx) {
    auto *shared = reinterpret_cast<MpvSharedState *>(ctx);
    QMutexLocker lock(&shared->mutex);
    if (shared->item)
        QMetaObject::invokeMethod(shared->item, "onMpvEvents", Qt::QueuedConnection);
}

void redrawCallback(void *ctx) {
    auto *shared = reinterpret_cast<MpvSharedState *>(ctx);
    QMutexLocker lock(&shared->mutex);
    if (shared->item)
        QMetaObject::invokeMethod(shared->item, "scheduleRedraw", Qt::QueuedConnection);
}
}

class MpvPlayerRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvPlayerRenderer(MpvPlayer *item)
        : m_item(item), m_shared(item->m_shared) {}

    ~MpvPlayerRenderer() override {
        // Живём в render thread; item мог уже умереть в main thread — работаем
        // только через shared state, без разыменования m_item.
        if (m_renderCtx) {
            mpv_render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
            mpv_render_context_free(m_renderCtx);
            m_renderCtx = nullptr;
        }
        mpv_handle *mpv = nullptr;
        {
            QMutexLocker lock(&m_shared->mutex);
            mpv = m_shared->mpv;
            m_shared->mpv = nullptr;
            if (m_shared->item)
                m_shared->item->m_mpv = nullptr;
        }
        if (mpv) {
            mpv_set_wakeup_callback(mpv, nullptr, nullptr);
            mpv_terminate_destroy(mpv); // join'ит потоки mpv — колбэки больше не придут
        }
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        // Единственное место, где render thread может безопасно трогать item
        // напрямую (GUI-поток заблокирован) — снимаем снапшот настроек.
        m_item = static_cast<MpvPlayer *>(item);
        m_renderMode = AppConfig::instance()->playerRenderMode();
    }

    void render() override {
        // item мог умереть с прошлого кадра — проверяем только через shared.
        {
            QMutexLocker lock(&m_shared->mutex);
            if (!m_shared->item || !m_shared->mpv)
                return;
        }

        if (!ensureRenderContext())
            return;

        const uint64_t flags = mpv_render_context_update(m_renderCtx);
        if (!(flags & MPV_RENDER_UPDATE_FRAME))
            return;

        if (m_useSoftware)
            renderSoftware();
        else
            renderOpenGl();

        // Без report_swap libmpv может копить внутреннюю очередь кадров —
        // RSS растёт по ходу просмотра (десятки МБ за минуты).
        mpv_render_context_report_swap(m_renderCtx);
    }

private:
    mpv_handle *sharedHandle() const {
        QMutexLocker lock(&m_shared->mutex);
        return m_shared->mpv;
    }

    // invokeMethod на item — только под мьютексом shared state.
    template <typename... Args>
    void invokeOnItem(const char *member, Args &&...args) {
        QMutexLocker lock(&m_shared->mutex);
        if (m_shared->item)
            QMetaObject::invokeMethod(m_shared->item, member, Qt::QueuedConnection,
                                      std::forward<Args>(args)...);
    }

    bool ensureRenderContext() {
        if (m_renderCtx)
            return true;
        if (!sharedHandle())
            return false;

        const QString mode = m_renderMode;
        if (mode != QLatin1String("software") && tryInitOpenGl()) {
            m_useSoftware = false;
            mpv_render_context_set_update_callback(m_renderCtx, redrawCallback, m_shared.get());
            invokeOnItem("setRenderBackend", Q_ARG(QString, QStringLiteral("opengl")));
            qInfo("MpvPlayer: GPU OpenGL render active");
            return true;
        }

        if (mode == QLatin1String("gpu")) {
            invokeOnItem("setRenderBackend", Q_ARG(QString, QStringLiteral("failed")));
            invokeOnItem("reportError",
                         Q_ARG(QString, QStringLiteral("GPU-рендер недоступен. Выберите «Авто» или «Программный» в настройках.")));
            qWarning("MpvPlayer: GPU render requested but unavailable");
            return false;
        }

        if (!initSoftware()) {
            invokeOnItem("setRenderBackend", Q_ARG(QString, QStringLiteral("failed")));
            invokeOnItem("reportError",
                         Q_ARG(QString, QStringLiteral("Не удалось инициализировать рендер mpv")));
            return false;
        }

        m_useSoftware = true;
        mpv_render_context_set_update_callback(m_renderCtx, redrawCallback, m_shared.get());
        invokeOnItem("setRenderBackend", Q_ARG(QString, QStringLiteral("software")));
        qInfo("MpvPlayer: software render active (GPU fallback)");
        return true;
    }

    bool tryInitOpenGl() {
        mpv_handle *mpv = sharedHandle();
        if (!mpv)
            return false;
        mpv_opengl_init_params glInit{mpvGlGetProcAddress, nullptr};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        return mpv_render_context_create(&m_renderCtx, mpv, params) >= 0;
    }

    bool initSoftware() {
        mpv_handle *mpv = sharedHandle();
        if (!mpv)
            return false;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m_renderCtx, mpv, params) < 0)
            return false;
        return true;
    }

    void renderOpenGl() {
        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!fbo)
            return;

        mpv_opengl_fbo mpvFbo{};
        mpvFbo.fbo = static_cast<int>(fbo->handle());
        mpvFbo.w = fbo->width();
        mpvFbo.h = fbo->height();
        int flip = 1;

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_renderCtx, params);
    }

    void renderSoftware() {
        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!fbo)
            return;

        int w = 0;
        int h = 0;
        MpvPlayer::computeRenderSize(fbo->width(), fbo->height(), &w, &h);

        if (m_swFrame.width() != w || m_swFrame.height() != h)
            m_swFrame = QImage(w, h, QImage::Format_RGB32);

        int size[2] = {w, h};
        static char format[] = "bgr0";
        size_t stride = static_cast<size_t>(m_swFrame.bytesPerLine());

        mpv_render_param swParams[] = {
            {MPV_RENDER_PARAM_SW_SIZE, size},
            {MPV_RENDER_PARAM_SW_FORMAT, format},
            {MPV_RENDER_PARAM_SW_STRIDE, &stride},
            {MPV_RENDER_PARAM_SW_POINTER, m_swFrame.bits()},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_renderCtx, swParams);

        fbo->bind();
        QOpenGLPaintDevice device(fbo->size());
        QPainter painter(&device);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.fillRect(QRect(0, 0, fbo->width(), fbo->height()), Qt::black);
        painter.drawImage(QRect(0, 0, fbo->width(), fbo->height()), m_swFrame);
        painter.end();
    }

    MpvPlayer *m_item = nullptr;
    std::shared_ptr<MpvSharedState> m_shared;
    QString m_renderMode;
    mpv_render_context *m_renderCtx = nullptr;
    bool m_useSoftware = true;
    QImage m_swFrame;
};

MpvPlayer::MpvPlayer(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    setMirrorVertically(true);

    m_shared = std::make_shared<MpvSharedState>();
    m_shared->item = this;

    m_redrawTimer = new QTimer(this);
    m_redrawTimer->setTimerType(Qt::CoarseTimer);
    m_redrawTimer->setSingleShot(true);
    m_redrawTimer->setInterval(kSwRedrawIntervalMs);
    connect(m_redrawTimer, &QTimer::timeout, this, &MpvPlayer::onMpvRedraw);

    connect(AppConfig::instance(), &AppConfig::playerFpsLimitChanged, this, [this]() {
        if (m_redrawTimer)
            m_redrawTimer->setInterval(redrawIntervalMs());
    });

    m_mpv = mpv_create();
    if (!m_mpv) {
        emit mpvError("Не удалось создать mpv_handle");
        return;
    }

    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "sw-fast", "yes");
    mpv_set_option_string(m_mpv, "interpolation", "no");
    mpv_set_option_string(m_mpv, "video-sync", "audio");
    mpv_set_option_string(m_mpv, "terminal", "no");
    // Без лимитов demuxer/network cache у HLS/CVH/торрентов: RAM сначала
    // быстро растёт (заполнение буфера), потом ползёт (ре-ахед/сегменты).
    // Жёсткий потолок буфера (байты — чтобы формат точно принялся).
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "50331648");      // 48 MiB
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "16777216"); // 16 MiB
    mpv_set_option_string(m_mpv, "demuxer-readahead-secs", "12");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "cache-secs", "15");
    // Меньше native surfaces у hwdec (NVIDIA/Wayland часто не отдаёт RAM).
    mpv_set_option_string(m_mpv, "hwdec-extra-frames", "2");
    mpv_set_option_string(m_mpv, "vd-lavc-dr", "no");
    // Не копить декодированные кадры сверх нужного для VO.
    mpv_set_option_string(m_mpv, "video-latency-hacks", "yes");
    mpv_set_option_string(m_mpv, "framedrop", "vo");
    if (qEnvironmentVariableIsSet("ANIME_CLIENT_MPV_LOG")) {
        mpv_set_option_string(m_mpv, "msg-level", "all=v");
        mpv_set_option_string(m_mpv, "log-file",
                               qEnvironmentVariable("ANIME_CLIENT_MPV_LOG").toUtf8().constData());
    }
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "osd-bar", "no");
    mpv_set_option_string(m_mpv, "cursor-autohide", "no");

    if (mpv_initialize(m_mpv) < 0) {
        emit mpvError("mpv_initialize() не удалась");
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    m_shared->mpv = m_mpv;
    mpv_set_wakeup_callback(m_mpv, wakeupCallback, m_shared.get());
    mpv_set_property_string(m_mpv, "playlist-play-auto", "no");

    // Повторно как property — на части сборок option до init игнорируется.
    mpv_set_property_string(m_mpv, "demuxer-max-bytes", "50331648");
    mpv_set_property_string(m_mpv, "demuxer-max-back-bytes", "16777216");
    mpv_set_property_string(m_mpv, "cache-secs", "15");
    mpv_set_property_string(m_mpv, "hwdec-extra-frames", "2");
    mpv_set_property_string(m_mpv, "vd-lavc-dr", "no");

    // time-pos НЕ observe: событие на каждый кадр → лавина QueuedConnection.
    // Позицию читаем таймером ~4 Гц (см. m_positionTimer).
    mpv_observe_property(m_mpv, PropDuration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropPause, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropVolume, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropMute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropSpeed, "speed", MPV_FORMAT_DOUBLE);

    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(kPositionEmitIntervalMs);
    m_positionTimer->setTimerType(Qt::CoarseTimer);
    connect(m_positionTimer, &QTimer::timeout, this, [this]() {
        if (!m_mpv || m_shuttingDown || !m_hasMedia)
            return;
        double pos = 0.0;
        if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0) {
            m_position = pos;
            emitPositionIfNeeded(pos);
        }
    });
    m_positionTimer->start();
}

MpvPlayer::~MpvPlayer() {
    m_shuttingDown = true;
    // Первым делом отписываем себя из shared state — с этого момента колбэки
    // mpv (wakeup/redraw) и render thread перестают ссылаться на item.
    {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->item = nullptr;
    }
    if (m_redrawTimer)
        m_redrawTimer->stop();
    if (m_geometryTimer)
        m_geometryTimer->stop();
    if (m_positionTimer)
        m_positionTimer->stop();
    if (m_mpv)
        mpv_command_string(m_mpv, "stop");
    // mpv_render_context must be freed before mpv_terminate_destroy — renderer does that
    // on the scene-graph thread; if GPU path was never used, tear down here.
    if (!m_rendererCreated && m_mpv) {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->mpv = nullptr;
        lock.unlock();
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

QQuickFramebufferObject::Renderer *MpvPlayer::createRenderer() const {
    auto *self = const_cast<MpvPlayer *>(this);
    self->m_rendererCreated = true;
    return new MpvPlayerRenderer(self);
}

void MpvPlayer::setRenderBackend(const QString &backend) {
    if (backend == m_renderBackend)
        return;
    m_renderBackend = backend;
    if (m_redrawTimer)
        m_redrawTimer->setInterval(redrawIntervalMs());
    emit renderBackendChanged();
}

void MpvPlayer::reportError(const QString &message) {
    emit mpvError(message);
}

int MpvPlayer::redrawIntervalMs() const {
    const QString limit = AppConfig::instance()->playerFpsLimit();
    if (limit == QLatin1String("unlimited"))
        return 0;
    if (limit == QLatin1String("120"))
        return 8;
    if (limit == QLatin1String("60"))
        return 17;
    if (limit == QLatin1String("30"))
        return kSwRedrawIntervalMs;
    // auto: GPU ~60 fps (безлимит давал лишние redraw + рост RAM драйвера
    // на части стеков Wayland/NVIDIA); CPU — 30 fps
    if (m_renderBackend == QLatin1String("opengl"))
        return 17;
    return kSwRedrawIntervalMs;
}

void MpvPlayer::scheduleRedraw() {
    if (m_shuttingDown || m_paused)
        return; // на паузе один кадр рисуется из applyPausedResourcePolicy
    const int intervalMs = redrawIntervalMs();
    if (intervalMs <= 0) {
        if (m_redrawPending)
            return;
        m_redrawPending = true;
        QMetaObject::invokeMethod(this, [this]() {
            m_redrawPending = false;
            onMpvRedraw();
        }, Qt::QueuedConnection);
        return;
    }
    if (!m_redrawTimer)
        return;
    m_redrawTimer->setInterval(intervalMs);
    if (!m_redrawTimer->isActive())
        m_redrawTimer->start();
}

void MpvPlayer::computeRenderSize(int displayW, int displayH, int *outW, int *outH) {
    int w = qMax(1, displayW);
    int h = qMax(1, displayH);
    const qint64 pixels = static_cast<qint64>(w) * h;
    if (pixels > kMaxSwRenderPixels) {
        const double scale = std::sqrt(static_cast<double>(kMaxSwRenderPixels) / static_cast<double>(pixels));
        w = qMax(2, static_cast<int>(w * scale)) & ~1;
        h = qMax(2, static_cast<int>(h * scale)) & ~1;
    }
    *outW = w;
    *outH = h;
}

void MpvPlayer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    if (!m_geometryTimer) {
        m_geometryTimer = new QTimer(this);
        m_geometryTimer->setSingleShot(true);
        m_geometryTimer->setInterval(80);
        connect(m_geometryTimer, &QTimer::timeout, this, &MpvPlayer::scheduleRedraw);
    }
    m_geometryTimer->start();
}

void MpvPlayer::onMpvRedraw() {
    update();
}

void MpvPlayer::emitPositionIfNeeded(double seconds) {
    if (m_lastEmittedPosition < 0.0
        || std::abs(seconds - m_lastEmittedPosition) >= kPositionEmitDelta
        || !m_positionEmitClock.isValid()
        || m_positionEmitClock.elapsed() >= kPositionEmitIntervalMs) {
        m_lastEmittedPosition = seconds;
        m_positionEmitClock.restart();
        emit positionChanged();
    }
}

void MpvPlayer::playUrl(const QString &url, const QString &title, const QString &proxyUrl,
                         const QString &referer) {
    if (!m_mpv)
        return;

    // Смена серии / источника: сначала выкидываем старый demuxer и external
    // audio-add (иначе RAM растёт от серии к серии на одном handle).
    flushPlayback();

    m_position = 0.0;
    m_duration = 0.0;
    m_hasMedia = false;
    m_loading = true;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    // Сброс "запомненной" дорожки — иначе на новой загрузке (Смэш, новая
    // серия/тайтл) updateTracksFromMpv() найдёт трек со случайно совпавшим
    // названием (например, торрентовский "по умолчанию") и молча вернёт
    // звук на него поверх track, выбранного через audio-add(select).
    m_preferredAudioTitle.clear();
    emit tracksChanged();
    if (!qFuzzyCompare(m_audioDelay, 0.0)) {
        // Сдвиг звука относился к прошлой связке видео+аудио (смэш) — на
        // новой загрузке (даже той же серии, но другого источника) он
        // не имеет смысла и должен сброситься.
        m_audioDelay = 0.0;
        emit audioDelayChanged();
        double zero = 0.0;
        mpv_set_property(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &zero);
    }
    m_lastEmittedPosition = -1.0;
    m_renderBackend = QStringLiteral("pending");
    emit renderBackendChanged();
    emit positionChanged();
    emit durationChanged();
    emit hasMediaChanged();
    emit loadingChanged();

    mpv_set_property_string(
        m_mpv, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    const bool cvhCdn = url.contains(QStringLiteral("okcdn.ru"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("vkuser.net"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("mycdn.me"), Qt::CaseInsensitive);
    if (cvhCdn)
        mpv_set_property_string(m_mpv, "referrer", "https://animego.org/");
    else if (!referer.isEmpty())
        mpv_set_property_string(m_mpv, "referrer", referer.toUtf8().constData());

    QString effectiveProxy = proxyUrl;
    if (cvhCdn)
        effectiveProxy.clear();

    const QByteArray proxyBytes = effectiveProxy.toUtf8();
    mpv_set_property(m_mpv, "http-proxy", MPV_FORMAT_NONE, nullptr);
    mpv_set_property(m_mpv, "proxy", MPV_FORMAT_NONE, nullptr);
    if (!effectiveProxy.isEmpty()) {
        if (effectiveProxy.startsWith(QStringLiteral("socks5"), Qt::CaseInsensitive))
            mpv_set_property_string(m_mpv, "proxy", proxyBytes.constData());
        else
            mpv_set_property_string(m_mpv, "http-proxy", proxyBytes.constData());
    }

    if (!title.isEmpty()) {
        const QByteArray titleBytes = title.toUtf8();
        mpv_set_property_string(m_mpv, "force-media-title", titleBytes.constData());
    }

    // После flushPlayback vid/hwdec выключены — вернуть для нового файла.
    mpv_set_property_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_property_string(m_mpv, "vid", "auto");
    mpv_set_property_string(m_mpv, "aid", "auto");

    m_pendingUrl = url.toUtf8();
    const char *cmd[] = {"loadfile", m_pendingUrl.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, CmdLoadFile, cmd);
}

void MpvPlayer::seek(double seconds) {
    if (!m_mpv || !m_hasMedia)
        return;
    m_pendingSeekArg = QByteArray::number(qMax(0.0, seconds));
    const char *cmd[] = {"seek", m_pendingSeekArg.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, CmdSeek, cmd);
}

void MpvPlayer::seekRelative(double seconds) {
    if (!m_mpv || !m_hasMedia)
        return;
    m_pendingSeekArg = QByteArray::number(seconds);
    const char *cmd[] = {"seek", m_pendingSeekArg.constData(), "relative", nullptr};
    mpv_command_async(m_mpv, CmdSeekRelative, cmd);
}

void MpvPlayer::setVolume(int v) {
    if (!m_mpv)
        return;
    v = qBound(0, v, 100);
    if (v == m_volume)
        return;
    m_volume = v;
    double vol = static_cast<double>(v);
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    emit volumeChanged();
}

void MpvPlayer::setMuted(bool m) {
    if (m == m_muted)
        return;
    m_muted = m;
    if (m_mpv) {
        int flag = m ? 1 : 0;
        mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
    }
    emit mutedChanged();
}

void MpvPlayer::setSpeed(double s) {
    if (!m_mpv)
        return;
    s = qBound(0.25, s, 4.0);
    if (qFuzzyCompare(s, m_speed))
        return;
    m_speed = s;
    mpv_set_property(m_mpv, "speed", MPV_FORMAT_DOUBLE, &s);
    emit speedChanged();
}

void MpvPlayer::stop() {
    if (!m_mpv)
        return;
    flushPlayback();
    m_position = 0.0;
    m_duration = 0.0;
    m_hasMedia = false;
    m_loading = false;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_preferredAudioTitle.clear();
    m_pendingUrl.clear();
    m_pendingAudioUrl.clear();
    if (!qFuzzyCompare(m_audioDelay, 0.0)) {
        m_audioDelay = 0.0;
        double zero = 0.0;
        mpv_set_property(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &zero);
        emit audioDelayChanged();
    }
    emit tracksChanged();
    emit hasMediaChanged();
    emit loadingChanged();
    emit positionChanged();
    emit durationChanged();
    update();
}

namespace {

QString nodeString(const mpv_node *node) {
    if (!node || node->format != MPV_FORMAT_STRING || !node->u.string)
        return {};
    return QString::fromUtf8(node->u.string);
}

int64_t nodeInt(const mpv_node *node) {
    if (!node)
        return -1;
    if (node->format == MPV_FORMAT_INT64)
        return node->u.int64;
    if (node->format == MPV_FORMAT_DOUBLE)
        return static_cast<int64_t>(node->u.double_);
    return -1;
}

bool nodeFlag(const mpv_node *node) {
    if (!node || node->format != MPV_FORMAT_FLAG)
        return false;
    return node->u.flag != 0;
}

const mpv_node *mapValue(const mpv_node *mapNode, const char *key) {
    if (!mapNode || mapNode->format != MPV_FORMAT_NODE_MAP || !mapNode->u.list)
        return nullptr;
    for (int i = 0; i < mapNode->u.list->num; ++i) {
        if (mapNode->u.list->keys[i] && std::strcmp(mapNode->u.list->keys[i], key) == 0)
            return &mapNode->u.list->values[i];
    }
    return nullptr;
}

} // namespace

void MpvPlayer::flushPlayback() {
    if (!m_mpv)
        return;

    // 1) Снять external tracks (audio-add / smash) — у каждого свой demuxer.
    mpv_node trackList{};
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &trackList) >= 0) {
        if (trackList.format == MPV_FORMAT_NODE_ARRAY && trackList.u.list) {
            QVector<int64_t> externalIds;
            for (int i = 0; i < trackList.u.list->num; ++i) {
                const mpv_node &t = trackList.u.list->values[i];
                if (t.format != MPV_FORMAT_NODE_MAP)
                    continue;
                const bool external = nodeFlag(mapValue(&t, "external"));
                const int64_t id = nodeInt(mapValue(&t, "id"));
                if (external && id >= 0)
                    externalIds.append(id);
            }
            for (int64_t id : externalIds) {
                const QByteArray idStr = QByteArray::number(static_cast<qlonglong>(id));
                const char *rm[] = {"track-remove", idStr.constData(), nullptr};
                mpv_command(m_mpv, rm);
            }
        }
        mpv_free_node_contents(&trackList);
    }

    // 2) Sync stop + очистка плейлиста (async stop + loadfile гонялись и
    //    оставляли буферы прошлой серии).
    mpv_command_string(m_mpv, "stop");
    mpv_command_string(m_mpv, "playlist-clear");

    // 3) Списки внешних файлов (если mpv что-то помнит)
    mpv_set_property_string(m_mpv, "audio-files", "");
    mpv_set_property_string(m_mpv, "sub-files", "");

    // 4) Выключить декодеры — пулы hwdec/ffmpeg часто не возвращают RSS
    //    пока vid/aid «живые». На playUrl loadfile снова поднимет потоки.
    mpv_set_property_string(m_mpv, "vid", "no");
    mpv_set_property_string(m_mpv, "aid", "no");
    mpv_set_property_string(m_mpv, "sid", "no");
    mpv_set_property_string(m_mpv, "hwdec", "no");

    m_pendingAudioUrl.clear();

#if defined(Q_OS_LINUX)
    // Отдать free-страницы ядру (glibc сам не отдаёт после ffmpeg/mpv).
    malloc_trim(0);
#endif
}

QString MpvPlayer::trackDisplayName(const mpv_node *trackMap) const {
    if (!trackMap)
        return QStringLiteral("Дорожка");

    const QString title = nodeString(mapValue(trackMap, "title"));
    if (!title.isEmpty())
        return title;

    const QString lang = nodeString(mapValue(trackMap, "lang"));
    if (!lang.isEmpty())
        return lang.toUpper();

    const QString codec = nodeString(mapValue(trackMap, "codec"));
    if (!codec.isEmpty())
        return codec.toUpper();

    const int64_t id = nodeInt(mapValue(trackMap, "id"));
    return id >= 0 ? QStringLiteral("#%1").arg(id) : QStringLiteral("Дорожка");
}

void MpvPlayer::updateTracksFromMpv() {
    if (!m_mpv) {
        m_audioTracks.clear();
        m_subtitleTracks.clear();
        emit tracksChanged();
        return;
    }

    mpv_node node;
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) {
        m_audioTracks.clear();
        m_subtitleTracks.clear();
        emit tracksChanged();
        return;
    }

    QVariantList audio;
    QVariantList subs;
    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node *entry = &node.u.list->values[i];
            if (!entry || entry->format != MPV_FORMAT_NODE_MAP)
                continue;

            const QString type = nodeString(mapValue(entry, "type"));
            if (type != QLatin1String("audio") && type != QLatin1String("sub"))
                continue;

            const int64_t id = nodeInt(mapValue(entry, "id"));
            if (id < 0)
                continue;

            QVariantMap track;
            track.insert(QStringLiteral("id"), static_cast<int>(id));
            track.insert(QStringLiteral("title"), trackDisplayName(entry));
            track.insert(QStringLiteral("selected"), nodeFlag(mapValue(entry, "selected")));
            if (type == QLatin1String("audio"))
                audio.append(track);
            else
                subs.append(track);
        }
    }
    mpv_free_node_contents(&node);

    int preferredId = -1;
    if (!m_preferredAudioTitle.isEmpty()) {
        for (const QVariant &v : audio) {
            const QVariantMap track = v.toMap();
            if (track.value(QStringLiteral("title")).toString() == m_preferredAudioTitle) {
                preferredId = track.value(QStringLiteral("id")).toInt();
                break;
            }
        }
    }

    if (preferredId >= 0) {
        int64_t aid = preferredId;
        mpv_set_property(m_mpv, "aid", MPV_FORMAT_INT64, &aid);
        qInfo("MpvPlayer: updateTracksFromMpv — переключаю aid на %d (preferredTitle=\"%s\")",
              preferredId, qUtf8Printable(m_preferredAudioTitle));
    }
    {
        QStringList dump;
        for (const QVariant &v : audio) {
            const QVariantMap t = v.toMap();
            dump << QString("id=%1 title=\"%2\" selected=%3")
                        .arg(t.value("id").toInt())
                        .arg(t.value("title").toString())
                        .arg(t.value("selected").toBool());
        }
        qInfo("MpvPlayer: audio tracks [%s]", qUtf8Printable(dump.join(" | ")));
    }

    m_audioTracks = audio;
    m_subtitleTracks = subs;

    if (preferredId >= 0) {
        for (QVariant &v : m_audioTracks) {
            QVariantMap track = v.toMap();
            track[QStringLiteral("selected")] = track.value(QStringLiteral("id")).toInt() == preferredId;
            v = track;
        }
    }

    emit tracksChanged();
}

void MpvPlayer::refreshTracks() {
    updateTracksFromMpv();
}

void MpvPlayer::setAudioTrack(int id) {
    if (!m_mpv || id < 0)
        return;
    for (const QVariant &v : m_audioTracks) {
        const QVariantMap track = v.toMap();
        if (track.value(QStringLiteral("id")).toInt() == id) {
            m_preferredAudioTitle = track.value(QStringLiteral("title")).toString();
            break;
        }
    }
    int64_t aid = id;
    mpv_set_property(m_mpv, "aid", MPV_FORMAT_INT64, &aid);
    updateTracksFromMpv();
}

void MpvPlayer::addExternalAudio(const QString &url, const QString &proxyUrl) {
    if (!m_mpv || !m_hasMedia)
        return;
    // http-proxy/proxy — глобальное свойство mpv, а не per-track. Ставим его
    // под конкретно эту дорожку (Kodik геоблокирован, торрент-видео — нет),
    // тот же приём, что и в playUrl() для основного файла.
    const QByteArray proxyBytes = proxyUrl.toUtf8();
    mpv_set_property(m_mpv, "http-proxy", MPV_FORMAT_NONE, nullptr);
    mpv_set_property(m_mpv, "proxy", MPV_FORMAT_NONE, nullptr);
    if (!proxyUrl.isEmpty()) {
        if (proxyUrl.startsWith(QStringLiteral("socks5"), Qt::CaseInsensitive))
            mpv_set_property_string(m_mpv, "proxy", proxyBytes.constData());
        else
            mpv_set_property_string(m_mpv, "http-proxy", proxyBytes.constData());
    }
    // Отдельная дорожка (Smash) идёт мимо playUrl(), где уже стоит referrer —
    // CDN Kodik (solodcdn и т.п.) обрывает соединение без Referer тем же
    // образом, что и основной поток, если это не CVH-CDN (там подпись в самом URL).
    const bool cvhCdn = url.contains(QStringLiteral("okcdn.ru"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("vkuser.net"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("mycdn.me"), Qt::CaseInsensitive);
    mpv_set_property_string(m_mpv, "referrer",
                             cvhCdn ? "https://animego.org/" : "https://kodikplayer.com/");
    qInfo("MpvPlayer: addExternalAudio host=%s (preferredTitle=\"%s\")",
          qUtf8Printable(QUrl(url).host()), qUtf8Printable(m_preferredAudioTitle));
    m_pendingAudioUrl = url.toUtf8();
    const char *cmd[] = {"audio-add", m_pendingAudioUrl.constData(), "select", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void MpvPlayer::setAudioDelay(double seconds) {
    if (!m_mpv)
        return;
    if (qFuzzyCompare(seconds, m_audioDelay))
        return;
    m_audioDelay = seconds;
    mpv_set_property(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &m_audioDelay);
    emit audioDelayChanged();
}

void MpvPlayer::setSubtitleTrack(int id) {
    if (!m_mpv)
        return;
    if (id < 0) {
        mpv_set_property_string(m_mpv, "sid", "no");
    } else {
        int64_t sid = id;
        mpv_set_property(m_mpv, "sid", MPV_FORMAT_INT64, &sid);
    }
    updateTracksFromMpv();
}

void MpvPlayer::applyPausedResourcePolicy(bool paused) {
    if (!m_mpv)
        return;
    if (paused) {
        // Страница тайтла + пауза: не держим 48 МБ readahead и не крутим render.
        mpv_set_property_string(m_mpv, "demuxer-max-bytes", "8388608");      // 8 MiB
        mpv_set_property_string(m_mpv, "demuxer-max-back-bytes", "4194304"); // 4 MiB
        mpv_set_property_string(m_mpv, "cache-secs", "2");
        if (m_redrawTimer)
            m_redrawTimer->stop();
        if (m_positionTimer)
            m_positionTimer->stop();
        m_redrawPending = false;
        update(); // один still-кадр
#if defined(Q_OS_LINUX)
        malloc_trim(0);
#endif
    } else {
        mpv_set_property_string(m_mpv, "demuxer-max-bytes", "50331648");
        mpv_set_property_string(m_mpv, "demuxer-max-back-bytes", "16777216");
        mpv_set_property_string(m_mpv, "cache-secs", "15");
        if (m_positionTimer && m_hasMedia)
            m_positionTimer->start();
        scheduleRedraw();
    }
}

void MpvPlayer::setPaused(bool p) {
    if (!m_mpv || p == m_paused)
        return;
    m_paused = p;
    int flag = p ? 1 : 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
    applyPausedResourcePolicy(p);
    emit pausedChanged();
}

void MpvPlayer::onMpvEvents() {
    if (!m_mpv || m_shuttingDown)
        return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_COMMAND_REPLY:
            if (event->error < 0 && event->reply_userdata == CmdLoadFile) {
                m_loading = false;
                m_hasMedia = false;
                emit loadingChanged();
                emit hasMediaChanged();
                emit mpvError(QString("Не удалось открыть поток: %1").arg(mpv_error_string(event->error)));
            } else if ((event->reply_userdata == CmdSeek || event->reply_userdata == CmdSeekRelative)
                       && event->error >= 0) {
                // Seek выполнен: на паузе pos-таймер остановлен
                // (applyPausedResourcePolicy), иначе прогресс-бар не двигался
                // бы до снятия паузы. Читаем позицию сразу после seek.
                double pos = 0.0;
                if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0) {
                    m_position = pos;
                    emitPositionIfNeeded(pos);
                }
            }
            break;
        case MPV_EVENT_END_FILE: {
            auto *data = static_cast<mpv_event_end_file *>(event->data);
            if (!data)
                break;
            if (data->reason == MPV_END_FILE_REASON_EOF) {
                m_hasMedia = false;
                emit hasMediaChanged();
                emit endOfFile();
            } else if (data->reason == MPV_END_FILE_REASON_ERROR) {
                m_loading = false;
                m_hasMedia = false;
                emit loadingChanged();
                emit hasMediaChanged();
                emit mpvError(QString("Воспроизведение прервано: %1").arg(mpv_error_string(data->error)));
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            m_loading = false;
            m_hasMedia = true;
            {
                int flag = 0;
                mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
                m_paused = false;
                applyPausedResourcePolicy(false);
                emit pausedChanged();
            }
            updateTracksFromMpv();
            emit loadingChanged();
            emit hasMediaChanged();
            scheduleRedraw();
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (!prop || !prop->data)
                break;
            switch (event->reply_userdata) {
            case PropTimePos:
                // legacy: time-pos больше не observe'ится (таймер), ветка на всякий
                m_position = *static_cast<double *>(prop->data);
                emitPositionIfNeeded(m_position);
                break;
            case PropDuration:
                m_duration = *static_cast<double *>(prop->data);
                emit durationChanged();
                break;
            case PropPause: {
                bool paused = *static_cast<int *>(prop->data) != 0;
                if (paused != m_paused) {
                    m_paused = paused;
                    applyPausedResourcePolicy(paused);
                    emit pausedChanged();
                }
                break;
            }
            case PropVolume: {
                int vol = static_cast<int>(*static_cast<double *>(prop->data));
                vol = qBound(0, vol, 100);
                if (vol != m_volume) {
                    m_volume = vol;
                    emit volumeChanged();
                }
                break;
            }
            case PropMute: {
                bool muted = *static_cast<int *>(prop->data) != 0;
                if (muted != m_muted) {
                    m_muted = muted;
                    emit mutedChanged();
                }
                break;
            }
            case PropSpeed: {
                double spd = *static_cast<double *>(prop->data);
                if (!qFuzzyCompare(spd, m_speed)) {
                    m_speed = spd;
                    emit speedChanged();
                }
                break;
            }
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
    }
}