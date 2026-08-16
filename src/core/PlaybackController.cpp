#include "PlaybackController.h"

#include <QVariantMap>

#include "AppConfig.h"
#include "HistoryManager.h"
#include "StreamReadiness.h"
#include "ThumbnailProbe.h"

namespace {

// Referer для стримов по источнику (единая логика для probe StreamReadiness
// и для mpv): Kodik CDN проверяет Referer на m3u8/сегментах, Animetka/AniLibria
// тоже. CVH-CDN обрабатывается отдельно (animego.org), пустая строка — не слать.
QString refererFor(const QString &translationId, const QString &url) {
    if (translationId.startsWith(QStringLiteral("kodik_")))
        return QStringLiteral("https://kodikplayer.com/");
    if (translationId.startsWith(QStringLiteral("animetka_"))
        || url.contains(QStringLiteral("animetka.com"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("notanime.ru"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("libria.fun"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("anilibria"), Qt::CaseInsensitive))
        return QStringLiteral("https://animetka.com/");
    return QString();
}

} // namespace

PlaybackController::PlaybackController(QObject *parent) : QObject(parent) {
    connect(&m_torrentManager, &TorrentStreamManager::statusChanged, this, &PlaybackController::setStatusMessage);
    connect(&m_torrentManager, &TorrentStreamManager::buffering, this, &PlaybackController::setBuffering);
    connect(&m_torrentManager, &TorrentStreamManager::streamReady, this, &PlaybackController::onStreamReady);
    // Часть ошибок TorrentStreamManager (таймаут списка файлов, окончательный
    // отказ addMagnet, отсутствие видео в раздаче) эмитит errorOccurred, не
    // трогая buffering — кнопка плеера так и оставалась на "LOADING..."
    // навсегда, хотя под ней уже был показан текст ошибки.
    connect(&m_torrentManager, &TorrentStreamManager::errorOccurred, this, [this](const QString &msg) {
        setBuffering(false);
        emit errorOccurred(msg);
    });
    connect(&m_torrentManager, &TorrentStreamManager::torrentFilesReady, this, &PlaybackController::torrentFilesReady);
}

void PlaybackController::attachPlayer(QObject *player) {
    auto *mpv = qobject_cast<MpvPlayer *>(player);
    if (!mpv) {
        emit errorOccurred("PlaybackController: переданный объект не является MpvPlayer");
        return;
    }
    if (m_player == mpv)
        return;
    if (m_player) {
        // Старый плеер больше не управляется контроллером — рвём все его
        // сигналы в контроллер. Иначе его EOF/позиция продолжали бы дёргать
        // контроллер, уже привязанный к новому плееру (двойная запись в
        // историю, ложный авто-переход на следующую серию).
        disconnect(m_player, nullptr, this, nullptr);
        m_player = nullptr;
    }
    m_player = mpv;
    connect(mpv, &MpvPlayer::hasMediaChanged, this, &PlaybackController::onPlayerHasMedia);
    connect(mpv, &MpvPlayer::positionChanged, this, &PlaybackController::onPlayerPosition);
    connect(mpv, &MpvPlayer::endOfFile, this, &PlaybackController::onPlayerEndOfFile);
    connect(mpv, &MpvPlayer::mpvError, this, &PlaybackController::errorOccurred);
}

void PlaybackController::setTotalEpisodes(int total) {
    total = qMax(0, total);
    if (total == m_totalEpisodes)
        return;
    m_totalEpisodes = total;
    emit totalEpisodesChanged();
}

void PlaybackController::openTitle(const QString &titleId, int totalEpisodes) {
    m_titleId = titleId;
    m_totalEpisodes = totalEpisodes;
    m_currentSource = Source::None;
    m_currentMagnet.clear();
    emit titleChanged();
    emit totalEpisodesChanged();

    QVariantMap progress = HistoryManager::instance()->loadProgress(titleId);
    if (progress.value("found").toBool()) {
        emit resumeAvailable(
            progress.value("episode").toInt(),
            progress.value("positionSeconds").toDouble(),
            progress.value("translationId").toString());
    } else {
        setEpisode(1);
    }
}

void PlaybackController::playTorrentEpisode(const QString &magnet, int episode,
                                            const QString &translationId) {
    ++m_playGeneration;

    if (m_player)
        m_player->stop();

    m_pendingResumeSeconds = -1.0;
    m_currentSource = Source::Torrent;
    m_currentMagnet = magnet;
    m_currentTranslationId = translationId.isEmpty() ? QStringLiteral("torrent") : translationId;
    // Смэш-хинт ставится вызывающим кодом (playSmashMixed) ПОСЛЕ этого вызова —
    // здесь всегда сбрасываем, иначе обычный "просто торрент" после Smash
    // унаследует чужую озвучку на границе EOF.
    m_smashAudioTranslationId.clear();
    setEpisode(episode);
    HistoryManager::instance()->setActive(
        m_titleId, m_currentEpisode,
        translationId.isEmpty() ? QStringLiteral("torrent") : translationId);
    setBuffering(true);
    setStatusMessage(QStringLiteral("Подготовка раздачи..."));
    m_torrentManager.play(magnet, episode, m_titleId);
}

void PlaybackController::playTorrentFile(const QString &hash, int fileId) {
    if (m_player)
        m_player->stop();
    m_pendingResumeSeconds = -1.0;
    setBuffering(true);
    m_torrentManager.playFile(hash, fileId);
}

void PlaybackController::playDirectUrl(const QString &url, int episode, bool useProxy,
                                       const QString &translationId) {
    ++m_playGeneration;
    const quint64 generation = m_playGeneration;

    if (m_player)
        m_player->stop();

    m_pendingResumeSeconds = -1.0;
    m_currentSource = Source::Direct;
    m_currentUseProxy = useProxy;
    m_smashAudioTranslationId.clear();
    m_currentTranslationId = translationId.isEmpty() ? QStringLiteral("direct") : translationId;
    setEpisode(episode);
    HistoryManager::instance()->setActive(
        m_titleId, m_currentEpisode,
        translationId.isEmpty() ? QStringLiteral("direct") : translationId);
    setBuffering(true);
    const bool isHls = url.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
    const bool isCvhCdn = url.contains(QStringLiteral("okcdn.ru"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("vkuser.net"), Qt::CaseInsensitive);
    if (isHls || isCvhCdn) {
        // CVH: okcdn отдаёт подписанные m3u8/mp4 — сразу в mpv без предпроверки.
        setBuffering(false);
        onStreamReady(url);
        return;
    }

    setStatusMessage("Проверка потока...");
    StreamReadiness::waitUntilReady(
        url,
        useProxy ? StreamReadiness::Route::External : StreamReadiness::Route::Local,
        refererFor(translationId, url),
        [this, url, generation](bool ok, int status) {
            if (generation != m_playGeneration)
                return;
            setBuffering(false);
            if (!ok) {
                emit errorOccurred(QString("Источник недоступен (HTTP %1). Возможно, нужен прокси или сервис геоблокирует регион.").arg(status));
                return;
            }
            onStreamReady(url);
        });
}

void PlaybackController::onStreamReady(const QString &url) {
    if (!m_player)
        return;
    QString proxyUrl;
    if (m_currentSource == Source::Direct && m_currentUseProxy)
        proxyUrl = AppConfig::instance()->mpvProxyUrl();
    // Торрент — локальный TorrServer, прокси на сам поток не нужен (и
    // вреден: localhost через внешний прокси ловит обрывы/502, см.
    // NetworkManager — тот же принцип применяется и здесь для mpv).
    // Kodik CDN проверяет Referer на m3u8/сегментах — без него отдаёт
    // ошибку, которую mpv репортит как "loading failed".
    const QString referer = refererFor(m_currentTranslationId, url);
    // Превью на таймлайне: тот же поток грузится на тихом handle, но лениво —
    // при первом наведении на таймлайн (requestThumbnail).
    m_currentThumbUrl = url;
    m_currentThumbReferer = referer;
    m_currentThumbProxyUrl = proxyUrl;
    m_player->playUrl(url, QString("Серия %1").arg(m_currentEpisode), proxyUrl, referer);
    setStatusMessage(QString());
}

void PlaybackController::applyResumePosition(double seconds) {
    if (seconds <= 1.0)
        return;
    if (m_player && m_player->hasMedia()) {
        m_player->seek(seconds);
    } else {
        // плеер ещё не открыл файл — встаём в очередь, применится в
        // onPlayerHasMedia как только начнётся реальное воспроизведение
        m_pendingResumeSeconds = seconds;
    }
}

void PlaybackController::onPlayerHasMedia() {
    if (!m_player || !m_player->hasMedia())
        return;
    if (m_pendingResumeSeconds > 1.0) {
        m_player->seek(m_pendingResumeSeconds);
        m_pendingResumeSeconds = -1.0;
    }
    if (m_hasPendingExternalAudio) {
        m_hasPendingExternalAudio = false;
        const QString proxyUrl = m_pendingExternalAudioUseProxy
            ? AppConfig::instance()->mpvProxyUrl() : QString();
        m_player->addExternalAudio(m_pendingExternalAudioUrl, proxyUrl);
        m_pendingExternalAudioUrl.clear();
    }
}

void PlaybackController::attachExternalAudio(const QString &url, bool useProxy) {
    if (url.isEmpty())
        return;
    if (m_player && m_player->hasMedia()) {
        const QString proxyUrl = useProxy ? AppConfig::instance()->mpvProxyUrl() : QString();
        m_player->addExternalAudio(url, proxyUrl);
    } else {
        // Видео (торрент) ещё грузится — применится в onPlayerHasMedia().
        m_hasPendingExternalAudio = true;
        m_pendingExternalAudioUrl = url;
        m_pendingExternalAudioUseProxy = useProxy;
    }
}

void PlaybackController::onPlayerPosition() {
    if (!m_player)
        return;
    // Контроллер — единственный, кто пишет позицию в БД; и QML, и сам
    // плеер читают её через player.position напрямую (один источник
    // правды вместо рассинхронизированных копий в трёх местах).
    HistoryManager::instance()->reportPosition(m_player->position());
}

void PlaybackController::setSmashAudioHint(const QString &audioTranslationId) {
    m_smashAudioTranslationId = audioTranslationId;
}

void PlaybackController::requestThumbnail(double seconds) {
    if (m_currentThumbUrl.isEmpty())
        return;
    ThumbnailProbe::instance()->loadIfNeeded(m_currentThumbUrl, m_currentThumbReferer, m_currentThumbProxyUrl);
    ThumbnailProbe::instance()->requestThumbnail(seconds);
}

void PlaybackController::onPlayerEndOfFile() {
    if (m_currentEpisode >= m_totalEpisodes)
        return;
    int nextEpisode = m_currentEpisode + 1;
    if (m_currentSource == Source::Torrent) {
        if (!m_smashAudioTranslationId.isEmpty()) {
            // Смэш: playTorrentEpisode тут ни при чём — нужен полный
            // playSmashMixed(episode, audio), иначе следующая серия
            // проиграется с оригинальной звуковой дорожкой торрента.
            setEpisode(nextEpisode);
            emit smashNextEpisodeNeeded(nextEpisode, m_smashAudioTranslationId);
            return;
        }
        playTorrentEpisode(m_currentMagnet, nextEpisode);
    } else {
        // Direct-источник — резолв следующей серии (Kodik/AniLibria) живёт
        // вне этого класса (каталог ещё не портирован); просим вызывающий
        // код снова вызвать playDirectUrl с новым URL.
        setEpisode(nextEpisode);
        emit nextEpisodeNeeded(nextEpisode);
    }
}

void PlaybackController::setStatusMessage(const QString &msg) {
    if (msg == m_statusMessage)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void PlaybackController::setBuffering(bool b) {
    if (b == m_buffering)
        return;
    m_buffering = b;
    emit bufferingChanged();
}

void PlaybackController::setEpisode(int episode) {
    if (episode < 1)
        episode = 1;
    if (episode == m_currentEpisode)
        return;
    m_currentEpisode = episode;
    emit episodeChanged();
}
