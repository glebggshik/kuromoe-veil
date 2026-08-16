#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include "MpvPlayer.h"
#include "TorrentStreamManager.h"

// Единая точка истины для "что сейчас смотрим" — связывает источник медиа
// (торрент/прямая ссылка), сам плеер и HistoryManager в один пайплайн, вместо
// того чтобы приложение (QML) и плеер независимо считали каждый свою серию/
// позицию и расходились (это и было фактической причиной "-1/26" в старой
// версии — состояние плейлиста жило в трёх местах одновременно).
//
// QML создаёт MpvPlayer как визуальный Item сам (он часть сцены), а сюда
// передаёт его через attachPlayer() — контроллер дальше управляет им, но не
// владеет визуальным деревом.
class PlaybackController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString titleId READ titleId NOTIFY titleChanged)
    Q_PROPERTY(int currentEpisode READ currentEpisode NOTIFY episodeChanged)
    Q_PROPERTY(int totalEpisodes READ totalEpisodes NOTIFY totalEpisodesChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)

public:
    explicit PlaybackController(QObject *parent = nullptr);

    Q_INVOKABLE void attachPlayer(QObject *player);

    // Открывает тайтл: подтягивает сохранённый прогресс из HistoryManager и,
    // если есть с чем продолжать, эмитит resumeAvailable — решение "продолжить
    // или начать с начала" остаётся за UI, бэкенд только предлагает.
    Q_INVOKABLE void openTitle(const QString &titleId, int totalEpisodes);

    // Лимит серий для авто-перехода и UI — у Kodik/AniLibria зависит от озвучки,
    // а не от общего числа серий на Шикимори.
    Q_INVOKABLE void setTotalEpisodes(int total);

    // Источник — торрент: полный пайплайн через TorrentStreamManager
    // (запуск TorrServer -> добавление магнета -> ожидание файлов ->
    // буферизация) перед тем, как URL попадёт в mpv.
    Q_INVOKABLE void playTorrentEpisode(const QString &magnet, int episode,
                                        const QString &translationId = QStringLiteral("torrent"));

    // Источник — прямая ссылка (Kodik/AniLibria), уже полученная вызывающим
    // кодом. Перед mpv она всё равно проходит ту же проверку готовности
    // (StreamReadiness), что и торрент-поток — раньше крэши на геоблоке/
    // мёртвом m3u8 ловились именно тут, в обход проверки.
    Q_INVOKABLE void playDirectUrl(const QString &url, int episode, bool useProxy,
                                   const QString &translationId = QStringLiteral("direct"));

    // Применяет позицию, предложенную через resumeAvailable, на текущей
    // загрузке (вызывается из QML после плей, либо до — встанет в очередь).
    Q_INVOKABLE void applyResumePosition(double seconds);

    // Воспроизвести конкретный файл из раздачи (после ручного выбора из torrentFilesReady).
    Q_INVOKABLE void playTorrentFile(const QString &hash, int fileId);

    // Смэш "видео с торрента + звук с Kodik/CVH": видео грузится через
    // playTorrentEpisode как обычно, а озвучка прикрепляется сюда отдельно —
    // если видео к этому моменту ещё не готово (hasMedia==false), откладываем
    // до onPlayerHasMedia().
    Q_INVOKABLE void attachExternalAudio(const QString &url, bool useProxy);

    // Помечает текущий playTorrentEpisode как часть Smash — при авто-переходе
    // на следующую серию (EOF) шлём smashNextEpisodeNeeded вместо тихого
    // playTorrentEpisode без звука. Пустая строка снимает пометку (обычный
    // торрент без Smash).
    Q_INVOKABLE void setSmashAudioHint(const QString &audioTranslationId);

    // Превью на таймлайне (#20): кадр снимается на отдельном тихом mpv_handle
    // (ThumbnailProbe), основной плеер не трогается. Вызывается из QML с
    // debounce (~100 мс). Кадр приходит через image://thumbs/frame.
    Q_INVOKABLE void requestThumbnail(double seconds);

    QString titleId() const { return m_titleId; }
    int currentEpisode() const { return m_currentEpisode; }
    int totalEpisodes() const { return m_totalEpisodes; }
    QString statusMessage() const { return m_statusMessage; }
    bool buffering() const { return m_buffering; }

signals:
    void titleChanged();
    void episodeChanged();
    void totalEpisodesChanged();
    void statusMessageChanged();
    void bufferingChanged();
    void errorOccurred(const QString &message);

    // found=true, episode/positionSeconds — последнее сохранённое; UI решает,
    // показывать ли плашку "продолжить" или просто использовать значения.
    void resumeAvailable(int episode, double positionSeconds, const QString &translationId);

    // mpv доиграл серию до конца, а дальше — либо это был торрент (контроллер
    // сам знает магнет и переключит следующую серию), либо прямая ссылка —
    // тогда резолв следующего URL не наша забота (Kodik/AniLibria ещё не
    // портированы), и мы просто просим вызывающий код его предоставить.
    void nextEpisodeNeeded(int episode);

    // Как nextEpisodeNeeded, но для Smash-торрента — вызывающий код должен
    // снова вызвать playSmashMixed(episode, audioTranslationId), а не просто
    // playTorrentEpisode (иначе новая серия останется без озвучки).
    void smashNextEpisodeNeeded(int episode, const QString &audioTranslationId);

    // Пробрасывается из TorrentStreamManager когда серия не найдена —
    // QML показывает список файлов раздачи для ручного выбора.
    void torrentFilesReady(const QVariantList &files);

private:
    enum class Source { None, Torrent, Direct };

    void setStatusMessage(const QString &msg);
    void setBuffering(bool b);
    void setEpisode(int episode);
    void onStreamReady(const QString &url);
    void onPlayerHasMedia();
    void onPlayerPosition();
    void onPlayerEndOfFile();

    QPointer<MpvPlayer> m_player;
    TorrentStreamManager m_torrentManager;

    QString m_titleId;
    int m_currentEpisode = 1;
    int m_totalEpisodes = 0;
    QString m_statusMessage;
    bool m_buffering = false;

    Source m_currentSource = Source::None;
    QString m_currentMagnet;     // для авто-продолжения следующей серии торрента
    QString m_currentTranslationId;
    bool m_currentUseProxy = false;
    // Для превью на таймлайне: тот же поток, что у основного плеера.
    QString m_currentThumbUrl;
    QString m_currentThumbReferer;
    QString m_currentThumbProxyUrl;

    double m_pendingResumeSeconds = -1.0; // -1 = нет отложенного резюме
    quint64 m_playGeneration = 0;         // отменяет устаревшие колбэки playDirectUrl

    bool m_hasPendingExternalAudio = false;
    QString m_pendingExternalAudioUrl;
    bool m_pendingExternalAudioUseProxy = false;

    // Непусто, если текущий торрент-плей — часть Smash (см. setSmashAudioHint).
    QString m_smashAudioTranslationId;
};
