#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>

#include <mpv/client.h>

// Отдаёт последний кадр ThumbnailProbe в QML: image://thumbs/frame?v=N
// (N = frameVersion, cache-busting при каждом новом кадре).
class ThumbnailImageProvider : public QQuickImageProvider {
public:
    ThumbnailImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Второй ТИХИЙ mpv_handle для превью на таймлайне. Основной плеер НЕ
// трогается (не seek-ается): превью ищется на отдельном handle с
// vo=null, aid/sid=no, pause=yes через seek + screenshot-raw.
//
// Жизненный цикл: load() с теми же url/referer/proxy, что у основного
// плеера → requestThumbnail(seconds) → seek (absolute, через
// mpv_command_async, ждём COMMAND_REPLY) → screenshot-raw (тоже
// COMMAND_REPLY) → парсим кадр → thumbnailUpdated.
// Кадр забирает ThumbnailImageProvider (image://thumbs/…).
class ThumbnailProbe : public QObject {
    Q_OBJECT
    // Номер кадра для cache-busting image://thumbs/frame?v=N.
    Q_PROPERTY(quint64 frameVersion READ frameVersion NOTIFY frameChanged)

public:
    explicit ThumbnailProbe(QObject *parent = nullptr);
    ~ThumbnailProbe() override;

    static ThumbnailProbe *instance();

    // Загружает файл на тихом handle (если url сменился с прошлого раза).
    Q_INVOKABLE void loadIfNeeded(const QString &url, const QString &referer, const QString &proxyUrl);
    // Запрос кадра на секунде. Пока файл не загрузился — запрос откладывается.
    Q_INVOKABLE void requestThumbnail(double seconds);
    // Остановить тихий handle (смена тайтла/закрытие плеера).
    Q_INVOKABLE void stop();

    QImage lastImage() const { return m_lastImage; }
    quint64 frameVersion() const { return m_frameVersion; }
    bool hasFile() const { return m_fileReady; }

signals:
    // QML обновляет image://thumbs/… (cache-busting по frameVersion()).
    void thumbnailUpdated(double seconds);
    void frameChanged();

private slots:
    void onMpvEvents();

private:
    static void wakeupCallback(void *ctx);
    void ensureLoaded(const QString &url, const QString &referer, const QString &proxyUrl);
    void doSeekAndShot();
    void sendScreenshotRequest();
    void handleCommandReply(mpv_event *event);
    void applyImage(const QImage &image, double seconds);

    mpv_handle *m_mpv = nullptr;
    QString m_loadedUrl;
    bool m_fileReady = false;
    double m_pendingSeconds = -1.0;
    QImage m_lastImage;
    quint64 m_frameVersion = 0;

    // Буферы команд держим живыми ДО COMMAND_REPLY: mpv_command_async не
    // гарантирует копирование args — url.toUtf8().constData() / стековый
    // char[] к моменту выполнения команды уже мусор (load/seek/screenshot
    // уходили пустыми, кадр не приходил).
    QByteArray m_loadUrlBytes;
    QByteArray m_seekArg;
    // Один seek+shot в полёте: пока не пришёл screenshot, новый seek не
    // слать (HLS/Kodik могут вообще не дать кадр — тогда просто нет кадра).
    bool m_shotInFlight = false;
    double m_shotSeconds = -1.0;

    enum : uint64_t {
        kCmdLoad = 1000,
        kCmdSeek = 1001,
        kCmdShot = 1002,
    };
};
