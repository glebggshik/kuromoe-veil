#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QQuickFramebufferObject>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <mpv/client.h>
#include <mpv/render.h>

class MpvPlayer;
class MpvPlayerRenderer;

// Состояние, разделяемое между MpvPlayer (main thread), MpvPlayerRenderer
// (render thread) и C-колбэками mpv (внутренние потоки mpv). Item и renderer
// умирают в разных потоках в недетерминированном порядке — прямые указатели
// друг на друга в колбэках/деструкторах давали гонку (dangling invokeMethod →
// "QEventDispatcherWin32::wakeUp: Failed to post a message" в логе).
// Колбэки mpv получают сырой указатель на MpvSharedState; он валиден до
// mpv_terminate_destroy (terminate join'ит потоки mpv), который выполняет
// последний владелец shared_ptr перед своим разрушением.
struct MpvSharedState {
    QMutex mutex;
    MpvPlayer *item = nullptr;   // nullptr после ~MpvPlayer
    mpv_handle *mpv = nullptr;   // владение: renderer (если создан), иначе item
};

// Встроенный mpv-плеер: GPU (OpenGL FBO) с автоматическим fallback на SW.
class MpvPlayer : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY hasMediaChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString renderBackend READ renderBackend NOTIFY renderBackendChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)

public:
    explicit MpvPlayer(QQuickItem *parent = nullptr);
    ~MpvPlayer() override;

    Renderer *createRenderer() const override;

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    bool hasMedia() const { return m_hasMedia; }
    bool loading() const { return m_loading; }
    QString renderBackend() const { return m_renderBackend; }
    void setPaused(bool p);

    Q_INVOKABLE void playUrl(const QString &url, const QString &title, const QString &proxyUrl,
                             const QString &referer = QString());
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshTracks();
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);
    // Прикрепляет внешний аудиопоток к уже загруженному видео (звук
    // Kodik/CVH поверх видео с торрента в режиме "смэш") и сразу делает
    // его активной дорожкой ("select").
    Q_INVOKABLE void addExternalAudio(const QString &url, const QString &proxyUrl);

    double audioDelay() const { return m_audioDelay; }
    void setAudioDelay(double seconds);

    QVariantList audioTracks() const { return m_audioTracks; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }

    int volume() const { return m_volume; }
    void setVolume(int v);
    double speed() const { return m_speed; }
    void setSpeed(double s);

    mpv_handle *handle() const { return m_mpv; }

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void hasMediaChanged();
    void loadingChanged();
    void volumeChanged();
    void speedChanged();
    void renderBackendChanged();
    void tracksChanged();
    void audioDelayChanged();
    void endOfFile();
    void mpvError(const QString &message);

public slots:
    void onMpvEvents();
    void scheduleRedraw();
    void setRenderBackend(const QString &backend);
    void reportError(const QString &message);

private slots:
    void onMpvRedraw();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    friend class MpvPlayerRenderer;
    void emitPositionIfNeeded(double seconds);
    // static: вызывается из render thread, когда item может быть уже мёртв.
    static void computeRenderSize(int displayW, int displayH, int *outW, int *outH);
    int redrawIntervalMs() const;
    void updateTracksFromMpv();
    QString trackDisplayName(const mpv_node *trackMap) const;
    // Sync: stop + playlist-clear + снять external audio (audio-add / smash).
    // Нужен при смене серии — иначе demuxer/кэш копятся на том же mpv_handle.
    void flushPlayback();
    // Пауза на странице тайтла: сжать demuxer, стопнуть redraw/pos-таймеры.
    void applyPausedResourcePolicy(bool paused);

    enum ObservedProperty : uint64_t {
        PropTimePos = 1,
        PropDuration = 2,
        PropPause = 3,
        PropVolume = 4,
        PropSpeed = 5,
    };

    enum CommandId : uint64_t {
        CmdLoadFile = 100,
        CmdSeek = 101,
        CmdSeekRelative = 102,
    };

    mpv_handle *m_mpv = nullptr;
    std::shared_ptr<MpvSharedState> m_shared;
    bool m_shuttingDown = false;
    bool m_rendererCreated = false;

    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = true;
    bool m_hasMedia = false;
    bool m_loading = false;
    QString m_renderBackend = QStringLiteral("pending");

    QByteArray m_pendingUrl;
    QByteArray m_pendingAudioUrl;
    QByteArray m_pendingSeekArg;
    double m_audioDelay = 0.0;
    QTimer *m_geometryTimer = nullptr;
    QTimer *m_redrawTimer = nullptr;
    QTimer *m_positionTimer = nullptr;
    bool m_redrawPending = false;
    QElapsedTimer m_positionEmitClock;
    double m_lastEmittedPosition = -1.0;
    int m_volume = 100;
    double m_speed = 1.0;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    QString m_preferredAudioTitle;
};