#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include "AniLibriaClient.h"
#include "AniListClient.h"
#include "JacRedClient.h"
#include "SukebeiClient.h"
#include "AnimegoClient.h"
#include "KodikClient.h"
#include "AnimetkaClient.h"
#include "HentasisClient.h"
#include "AnistarClient.h"
#include "PlaybackController.h"
#include "ShikimoriClient.h"

// Мост для экрана деталей — порт qt_bridge/detail_bridge.py. Метаданные/
// статус/торренты/AniLibria/CVH/related — здесь; САМО воспроизведение
// делегируется PlaybackController (через attachPlaybackController).
class DetailBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentStatus READ currentStatus NOTIFY statusChanged)
    // Per-source состояние загрузки озвучек для UI: source -> {state, message},
    // state: "loading" | "ok" | "empty" | "error". Источники: cvh, kodik,
    // animetka, hentasis, anistar. Показываем ошибку/пустоту не только в логе.
    Q_PROPERTY(QVariantMap sourceStatus READ sourceStatus NOTIFY sourceStatusChanged)

public:
    explicit DetailBridge(QObject *parent = nullptr);

    Q_INVOKABLE void attachPlaybackController(QObject *controller);

    Q_INVOKABLE void load(const QVariant &item);
    Q_INVOKABLE void searchTorrents();
    Q_INVOKABLE void selectTorrent(const QString &magnet);
    Q_INVOKABLE void setStatus(const QString &status);
    Q_INVOKABLE void play(int episode, const QString &translationId);

    // "Смэш": видео — с торрента (лучше качество, чем у прямых потоков
    // Kodik/CVH), а звук — с выбранной озвучки Kodik/CVH поверх него.
    Q_INVOKABLE void playSmashMixed(int episode, const QString &audioTranslationId);

    QString currentStatus() const;
    QVariantMap sourceStatus() const { return m_sourceStatus; }

signals:
    void detailsReady(const QVariant &item);
    void translationsReady(const QVariantList &list);
    void anilibriaReady(bool available);
    void anilibriaEpisodesReady(int count);
    void relatedReady(const QVariantList &items);
    void statusChanged();
    void sourceStatusChanged();
    void torrentsReady(const QVariantList &list);
    void torrentsLoading(bool loading);
    void progressReady(int episode, const QString &translationId, const QString &torrentMagnet);
    void error(const QString &message);

private:
    void emitMergedTorrents();
    void emitMergedTranslations();
    void loadCvh();
    void loadKodik();
    void loadAnimetka();
    void loadHentaiSources();
    void setSourceStatus(const QString &source, const QString &state, const QString &message);
    QVariantMap m_sourceStatus;

    ShikimoriClient m_shikimori;
    AniLibriaClient m_anilibria;
    AniListClient m_anilist;
    JacRedClient m_jacred;
    SukebeiClient m_sukebei;
    AnimegoClient m_animego;
    KodikClient m_kodik;
    AnimetkaClient m_animetka;
    HentasisClient m_hentasis;
    AnistarClient m_anistar;

    QString m_animegoId;
    QVariantList m_cvhTranslations;
    QVariantList m_kodikTranslations;
    QVariantList m_animetkaTranslations;
    QVariantList m_hentasisTranslations;
    QVariantList m_anistarTranslations;
    int m_kodikGen = 0;
    int m_animetkaGen = 0;
    int m_hentaiSourcesGen = 0;
    // Единое поколение загрузки страницы: режет ВСЕ колбэки load()
    // (Shikimori getDetails, AniList banner/titles, AniLibria, related) —
    // иначе быстрый переход A→B применял к тайтлу B результаты A.
    int m_loadGen = 0;
    bool m_kodikReady = false;
    bool m_animetkaReady = false;
    bool m_hentasisReady = false;
    bool m_anistarReady = false;

    QPointer<PlaybackController> m_playback;

    QVariantMap m_item;
    QVariantMap m_anilibriaRelease;
    bool m_anilibriaAvailable = false;
    QString m_lastTorrentMagnet;
    QVariantList m_jacredTorrents;
    QVariantList m_anistarTorrents;
    bool m_jacredSearchDone = false;
    bool m_deferredTorrentSearch = false;
    int m_torrentSearchGen = 0;
    // Инкрементируется на каждый play()/playSmashMixed() — резолв URL
    // (getEpisodeStream) асинхронный, и без проверки поколения быстрый клик
    // "серия 5 -> серия 3" может применить результат серии 5 уже после 3.
    int m_playGen = 0;
    int m_cvhGen = 0;
    bool m_cvhReady = false;
    // Ранний CVH-запуск + retry после обогащения названий: пока поиск идёт,
    // повторные loadCvh() не перезапускают его (лишний трафик к Animego —
    // риск бана), а откладываются до завершения текущего.
    bool m_cvhInFlight = false;
    bool m_cvhRetryQueued = false;
};