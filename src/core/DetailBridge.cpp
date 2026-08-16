#include "DetailBridge.h"

#include <algorithm>
#include <memory>

#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include "HistoryManager.h"
#include "PosterCache.h"
#include "ShikimoriClient.h"
#include "StatusStore.h"
#include "TorrentRanking.h"

namespace {
// Поиск/ранжирование торрентов вынесены в TorrentRanking.{h,cpp} (unit-тесты).
using namespace torrentRanking;

constexpr int kHentaiGenreId = 12;
constexpr int kTorrentMinScoreDefault = 12;
constexpr int kTorrentMinScoreHentai = 3;

// Кэш результатов поиска торрентов по titleId — живёт в статике процесса, то
// есть до закрытия приложения (не персистится на диск). Поиск через JacRed/
// Sukebei медленный (несколько последовательных запросов с паузами против
// рейт-лимита, см. jacredJitterMs) — при повторном заходе на тот же тайтл
// (возврат назад, "Связанное", смена и снова тот же источник) нет смысла
// гонять его заново.
QHash<QString, QVariantList> g_torrentSearchCache;

// jac.red агрессивно рейт-лимитит (429 Too Many Requests) уже на ВТОРОМ
// запросе, если слать их без паузы — а поиск торрентов делает несколько
// последовательных запросов с разными вариантами названия (см. runQueryAt
// в searchTorrents). Без задержки реально находились только результаты
// первого запроса — все "уточняющие" запросы молча падали в 429 и
// возвращали 0, из-за чего для многих тайтлов торренты просто не
// показывались, хотя раздачи на самом деле есть.
int jacredJitterMs(int minMs, int maxMs) {
    if (maxMs <= minMs)
        return minMs;
    return minMs + static_cast<int>(QRandomGenerator::global()->bounded(maxMs - minMs));
}

bool isHentaiItem(const QVariantMap &item) {
    for (const QVariant &v : item.value(QStringLiteral("genreTags")).toList()) {
        if (v.toMap().value(QStringLiteral("id")).toInt() == kHentaiGenreId)
            return true;
    }
    return false;
}

}

DetailBridge::DetailBridge(QObject *parent) : QObject(parent) {}

void DetailBridge::attachPlaybackController(QObject *controller) {
    m_playback = qobject_cast<PlaybackController *>(controller);
    if (!m_playback) {
        emit error("DetailBridge: переданный объект не является PlaybackController");
        return;
    }
    // Smash + авто-переход на следующую серию: PlaybackController не умеет
    // сам резолвить озвучку Kodik/CVH — при EOF на смэш-торренте он просит
    // нас повторить playSmashMixed целиком (видео+звук), а не только видео.
    connect(m_playback, &PlaybackController::smashNextEpisodeNeeded, this,
            [this](int episode, const QString &audioTranslationId) {
                playSmashMixed(episode, audioTranslationId);
            });
}

QString DetailBridge::currentStatus() const {
    return StatusStore::instance()->currentStatus(m_item.value("id").toString());
}

void DetailBridge::setSourceStatus(const QString &source, const QString &state, const QString &message) {
    QVariantMap entry;
    entry[QStringLiteral("state")] = state;
    entry[QStringLiteral("message")] = message;
    if (m_sourceStatus.value(source).toMap() == entry)
        return;
    m_sourceStatus[source] = entry;
    emit sourceStatusChanged();
}

void DetailBridge::load(const QVariant &item) {
    m_item = item.toMap();
    QString id = m_item.value("id").toString();
    if (id.isEmpty())
        return;

    m_anilibriaRelease = QVariantMap();
    m_anilibriaAvailable = false;
    m_animegoId.clear();
    m_lastTorrentMagnet.clear();
    m_jacredTorrents.clear();
    m_jacredSearchDone = false;
    m_deferredTorrentSearch = false;
    m_cvhReady = false;
    m_cvhInFlight = false;
    m_cvhRetryQueued = false;
    m_kodikReady = false;
    m_animetkaReady = false;
    m_cvhTranslations.clear();
    m_kodikTranslations.clear();
    m_animetkaTranslations.clear();
    m_hentasisTranslations.clear();
    m_anistarTranslations.clear();
    m_anistarTorrents.clear();
    m_hentasisReady = false;
    m_anistarReady = false;
    ++m_cvhGen;
    ++m_kodikGen;
    ++m_animetkaGen;
    ++m_hentaiSourcesGen;
    ++m_loadGen;
    m_sourceStatus.clear();
    emit sourceStatusChanged();
    emit statusChanged();

    QVariantMap progress = HistoryManager::instance()->loadProgress(id);
    QVariantMap torrentInfo = StatusStore::instance()->getTorrent(id);
    m_lastTorrentMagnet = torrentInfo.value("magnet").toString();
    if (progress.value("found").toBool()) {
        emit progressReady(
            progress.value("episode").toInt(),
            progress.value("translationId").toString(),
            m_lastTorrentMagnet);
    }

    const bool hentai = isHentaiItem(m_item);
    searchTorrents();
    if (hentai) {
        loadKodik();
        loadAnimetka();
        loadHentaiSources();
    } else {
        loadKodik();
        loadAnimetka();
        // CVH стартует сразу с данными карточки (title/originalTitle/kind/year
        // уже есть из каталога) — раньше он ждал полного ответа Shikimori
        // getDetails (1-3+ сек), из-за чего озвучки CVH появлялись заметно
        // позже Kodik. После getDetails/AniList остаётся только retry с
        // обогащёнными названиями, если первая попытка не нашла (см. ниже).
        loadCvh();
    }

    QPointer<DetailBridge> self(this);
    // Поколение текущей загрузки — все колбэки ниже (Shikimori/AniList/
    // AniLibria/related) режутся по нему, чтобы при быстром открытии
    // тайтла B результаты тайтла A не применялись к нему.
    const int loadGen = m_loadGen;
    m_shikimori.getDetails(id, [self, id, loadGen](QVariantMap details, QString err) {
        if (!self || loadGen != self->m_loadGen)
            return;
        if (!err.isEmpty() || details.isEmpty())
            return;
        for (auto it = details.constBegin(); it != details.constEnd(); ++it)
            self->m_item[it.key()] = it.value();
        emit self->detailsReady(self->m_item);
        if (self->m_playback)
            self->m_playback->openTitle(id, self->m_item.value("episodes").toInt());
        // Retry CVH с деталями Shikimori только если ранний запуск (см. load())
        // ещё не нашёл озвучки — не дёргаем Animego повторно без нужды.
        if (!isHentaiItem(self->m_item) && !self->m_cvhReady)
            self->loadCvh();

        const int malId = self->m_item.value(QStringLiteral("malId")).toInt();
        if (malId <= 0)
            return;

        // Кэшируем AniList hero-баннер в фоне ПРЯМО СЕЙЧАС, пока пользователь
        // открыл тайтл и смотрит серию — а не только когда он уже показан в
        // hero на Главной. Раньше баннер докачивался лишь в момент показа на
        // Главной (часто впервые при следующем заходе в приложение), и пока
        // шла загрузка/была неудачной (см. "download stalled" в логе), hero
        // молча подменялся постером Shikimori. Так к моменту возврата на
        // Главную баннер уже почти наверняка на диске.
        const QString existingBanner = self->m_item.value(QStringLiteral("heroBanner")).toString();
        if (!existingBanner.isEmpty()) {
            PosterCache::instance()->requestPriority(existingBanner);
        } else {
            const QString title = self->m_item.value(QStringLiteral("title")).toString();
            const QString originalTitle = self->m_item.value(QStringLiteral("originalTitle")).toString();
            self->m_anilist.fetchBanner(malId, title, originalTitle, [self, loadGen](QString bannerUrl) {
                if (!self || loadGen != self->m_loadGen || bannerUrl.isEmpty())
                    return;
                self->m_item[QStringLiteral("heroBanner")] = bannerUrl;
                PosterCache::instance()->requestPriority(bannerUrl);
                // Баннер приходит асинхронно, позже основного detailsReady — без
                // повторного эмита QML-сторона (heroBanner-фон в шапке деталей)
                // никогда не узнаёт, что он появился, и остаётся без широкого
                // фона. Переприсваивание safe — тот же паттерн, что и один emit.
                emit self->detailsReady(self->m_item);
            });
        }

        self->m_anilist.fetchTitles(malId, [self, loadGen](AniListClient::MediaTitles titles) {
            if (!self || loadGen != self->m_loadGen)
                return;
            bool enriched = false;
            if (!titles.english.isEmpty()
                && self->m_item.value(QStringLiteral("englishTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("englishTitle")] = titles.english;
                enriched = true;
            }
            if (!titles.native.isEmpty()
                && self->m_item.value(QStringLiteral("japaneseTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("japaneseTitle")] = titles.native;
                enriched = true;
            }
            if (!titles.romaji.isEmpty()
                && self->m_item.value(QStringLiteral("originalTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("originalTitle")] = titles.romaji;
                enriched = true;
            }
            if (enriched && !self->m_cvhReady)
                self->loadCvh();
            if (isHentaiItem(self->m_item) && enriched) {
                self->loadHentaiSources();
                if (self->m_jacredTorrents.isEmpty()) {
                    if (!self->m_jacredSearchDone)
                        self->m_deferredTorrentSearch = true;
                    else
                        self->searchTorrents();
                }
            }
            if (!enriched || !self->m_jacredTorrents.isEmpty())
                return;
            if (!self->m_jacredSearchDone) {
                self->m_deferredTorrentSearch = true;
                return;
            }
            self->searchTorrents();
        });
    });

    if (!hentai) {
        m_anilibria.findRelease(
            m_item.value(QStringLiteral("title")).toString(),
            m_item.value(QStringLiteral("originalTitle")).toString(),
            m_item.value(QStringLiteral("kind")).toString(),
            m_item.value(QStringLiteral("year")).toInt(),
            [self, loadGen](QVariantMap release, QString err) {
        if (!self || loadGen != self->m_loadGen)
            return;
        const QString title = self->m_item.value(QStringLiteral("title")).toString();
        if (!err.isEmpty())
            qWarning("DetailBridge: AniLibria failed for '%s' — %s", qUtf8Printable(title), qUtf8Printable(err));
        else if (release.isEmpty())
            qWarning("DetailBridge: AniLibria not found for '%s'", qUtf8Printable(title));
        else
            qInfo("DetailBridge: AniLibria ready for '%s' (id=%d)", qUtf8Printable(title), release.value("id").toInt());
        self->m_anilibriaRelease = release;
        self->m_anilibriaAvailable = !release.isEmpty();
        emit self->anilibriaReady(self->m_anilibriaAvailable);
        if (self->m_anilibriaAvailable) {
            int maxEpisode = 0;
            for (const QVariant &ev : AniLibriaClient::getEpisodes(self->m_anilibriaRelease))
                maxEpisode = qMax(maxEpisode, ev.toMap().value("episode").toInt());
            emit self->anilibriaEpisodesReady(maxEpisode);
        }
        self->emitMergedTorrents();
        });
    } else {
        m_anilibriaAvailable = false;
        emit anilibriaReady(false);
    }

    m_shikimori.getRelated(id, [self, loadGen](QVariantList items, QString err) {
        if (self && loadGen == self->m_loadGen && err.isEmpty())
            emit self->relatedReady(items);
    });
}

void DetailBridge::loadCvh() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    // Поиск уже идёт — не перезапускаем (Animego чувствителен к частоте
    // запросов), а откладываем retry до завершения текущей попытки.
    if (m_cvhInFlight) {
        m_cvhRetryQueued = true;
        return;
    }
    m_cvhInFlight = true;
    setSourceStatus(QStringLiteral("cvh"), QStringLiteral("loading"), QString());

    ++m_cvhGen;
    const int gen = m_cvhGen;

    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString().trimmed();
    const QString kind = m_item.value(QStringLiteral("kind")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    QPointer<DetailBridge> self(this);
    m_animego.loadTranslations(title, originalTitle, englishTitle, kind, year,
                               [self, id, gen](QVariantList list, QString err) {
                                   if (!self || gen != self->m_cvhGen)
                                       return;
                                   self->m_cvhInFlight = false;
                                   if (!err.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("error"), err);
                                       qWarning("DetailBridge: CVH failed for %s — %s", qUtf8Printable(id),
                                                qUtf8Printable(err));
                                       // Не шлём в общий statusText — это бы показывало ошибку CVH,
                                       // даже когда пользователь выбрал другой источник (Kodik/AniLibria/
                                       // торрент). Состояние "озвучки не найдены" уже отображается
                                       // отдельным индикатором под кнопкой CVH, видимым только когда
                                       // CVH выбран — см. DetailView.qml.
                                   } else if (list.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("empty"), QString());
                                       qWarning("DetailBridge: CVH empty translations for %s", qUtf8Printable(id));
                                   } else {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("ok"), QString());
                                       self->m_cvhReady = true;
                                       self->m_animegoId = list.first().toMap().value(QStringLiteral("animegoId")).toString();
                                       qInfo("DetailBridge: CVH ready for %s (animego=%s, %d voices)",
                                             qUtf8Printable(id), qUtf8Printable(self->m_animegoId), list.size());
                                   }
                                   self->m_cvhTranslations = list;
                                   self->emitMergedTranslations();
                                   // Отложенный retry (getDetails/AniList успели обогатить
                                   // названия, пока шёл первый поиск) — только если ничего
                                   // не нашли; повторный поиск возьмёт свежий m_item.
                                   if (self->m_cvhRetryQueued) {
                                       self->m_cvhRetryQueued = false;
                                       if (!self->m_cvhReady)
                                           self->loadCvh();
                                   }
                               });
}

void DetailBridge::loadKodik() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    ++m_kodikGen;
    const int gen = m_kodikGen;
    setSourceStatus(QStringLiteral("kodik"), QStringLiteral("loading"), QString());

    QPointer<DetailBridge> self(this);
    m_kodik.loadTranslations(id, [self, id, gen](QVariantList list, QString err) {
        if (!self || gen != self->m_kodikGen)
            return;
        if (!err.isEmpty()) {
            self->setSourceStatus(QStringLiteral("kodik"), QStringLiteral("error"), err);
            qWarning("DetailBridge: Kodik failed for %s — %s", qUtf8Printable(id), qUtf8Printable(err));
        } else if (list.isEmpty()) {
            self->setSourceStatus(QStringLiteral("kodik"), QStringLiteral("empty"), QString());
            qWarning("DetailBridge: Kodik empty translations for %s", qUtf8Printable(id));
        } else {
            self->m_kodikReady = true;
            qInfo("DetailBridge: Kodik ready for %s (%d voices)", qUtf8Printable(id), list.size());
        }
        // Префиксуем id "kodik_", чтобы play()/QML отличали озвучку Kodik от
        // CVH ("cvh_...") при общем списке translationsReady — оба источника
        // показываются и выбираются параллельно, не вытесняя друг друга.
        QVariantList prefixed;
        for (const QVariant &v : list) {
            QVariantMap m = v.toMap();
            m[QStringLiteral("id")] = QStringLiteral("kodik_") + m.value(QStringLiteral("id")).toString();
            prefixed << m;
        }
        self->m_kodikTranslations = prefixed;
        self->emitMergedTranslations();
    });
}

void DetailBridge::loadAnimetka() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    ++m_animetkaGen;
    const int gen = m_animetkaGen;
    setSourceStatus(QStringLiteral("animetka"), QStringLiteral("loading"), QString());

    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    QPointer<DetailBridge> self(this);
    m_animetka.loadTranslations(id, title, originalTitle, englishTitle, year,
                                [self, id, gen](QVariantList list, QString err) {
                                    if (!self || gen != self->m_animetkaGen)
                                        return;
                                    if (!err.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("animetka"), QStringLiteral("error"), err);
                                        qWarning("DetailBridge: Animetka failed for %s — %s",
                                                 qUtf8Printable(id), qUtf8Printable(err));
                                    } else if (list.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("animetka"), QStringLiteral("empty"), QString());
                                        qWarning("DetailBridge: Animetka empty for %s", qUtf8Printable(id));
                                    } else {
                                        self->m_animetkaReady = true;
                                        qInfo("DetailBridge: Animetka ready for %s (%d voices)",
                                              qUtf8Printable(id), list.size());
                                    }
                                    // id уже с префиксом animetka_ из клиента
                                    self->m_animetkaTranslations = list;
                                    self->emitMergedTranslations();
                                });
}

void DetailBridge::emitMergedTranslations() {
    emit translationsReady(m_cvhTranslations + m_kodikTranslations + m_animetkaTranslations
                           + m_hentasisTranslations + m_anistarTranslations);
}

void DetailBridge::loadHentaiSources() {
    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString();
    const QString japaneseTitle = m_item.value(QStringLiteral("japaneseTitle")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    ++m_hentaiSourcesGen;
    const int gen = m_hentaiSourcesGen;
    setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("loading"), QString());
    setSourceStatus(QStringLiteral("anistar"), QStringLiteral("loading"), QString());

    QPointer<DetailBridge> self(this);
    m_hentasis.loadTranslations(title, originalTitle, englishTitle, japaneseTitle, year,
                                [self, gen](QVariantList list, QString err) {
                                    if (!self || gen != self->m_hentaiSourcesGen)
                                        return;
                                    if (!err.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("error"), err);
                                        qWarning("DetailBridge: Hentasis failed — %s", qUtf8Printable(err));
                                    } else if (list.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("empty"), QString());
                                        qWarning("DetailBridge: Hentasis not found");
                                    } else {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("ok"), QString());
                                        qInfo("DetailBridge: Hentasis ready (%d tracks)", list.size());
                                    }
                                    self->m_hentasisReady = !list.isEmpty();
                                    self->m_hentasisTranslations = list;
                                    self->emitMergedTranslations();
                                });

    m_anistar.loadTranslations(title, originalTitle, englishTitle, japaneseTitle, year,
                               [self, gen, title, originalTitle, englishTitle, japaneseTitle, year](
                                   QVariantList list, QString err) {
                                   if (!self || gen != self->m_hentaiSourcesGen)
                                       return;
                                   if (!err.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("error"), err);
                                       qWarning("DetailBridge: AniStar stream failed — %s", qUtf8Printable(err));
                                   } else if (list.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("empty"), QString());
                                       qInfo("DetailBridge: AniStar direct stream not found");
                                   } else {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("ok"), QString());
                                       qInfo("DetailBridge: AniStar stream ready (%d tracks)", list.size());
                                   }
                                   self->m_anistarReady = !list.isEmpty();
                                   self->m_anistarTranslations = list;
                                   self->emitMergedTranslations();

                                   self->m_anistar.fetchPageTorrents(
                                       title, originalTitle, englishTitle, japaneseTitle, year,
                                       [self, gen](QVariantList torrents, QString terr) {
                                           if (!self || gen != self->m_hentaiSourcesGen)
                                               return;
                                           if (!terr.isEmpty())
                                               qWarning("DetailBridge: AniStar torrents — %s", qUtf8Printable(terr));
                                           self->m_anistarTorrents.clear();
                                           for (const QVariant &tv : torrents) {
                                               const QVariantMap t = tv.toMap();
                                               if (!t.value(QStringLiteral("magnet")).toString().isEmpty())
                                                   self->m_anistarTorrents << t;
                                           }
                                           if (!self->m_anistarTorrents.isEmpty())
                                               qInfo("DetailBridge: AniStar torrents %d",
                                                     self->m_anistarTorrents.size());
                                           if (self->m_jacredSearchDone)
                                               self->emitMergedTorrents();
                                       });
                               });
}

void DetailBridge::emitMergedTorrents() {
    if (!m_jacredSearchDone)
        return;

    const QString title = m_item.value("title").toString();
    const int year = m_item.value("year").toInt();
    const QString kind = m_item.value("kind").toString();

    QVariantList combined;
    QSet<QString> seenMagnets;
    for (const QVariant &tv : AniLibriaClient::getTorrents(m_anilibriaRelease)) {
        QVariantMap t = tv.toMap();
        QString label = t.value("label").toString();
        if (label.isEmpty())
            label = title;
        if (!matchesYear(label, year) || !matchesKind(label, kind))
            continue;
        const QString magnet = t.value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        QVariantMap out;
        out["title"] = label;
        out["magnet"] = magnet;
        const double sizeGb = t.value("sizeGb").toDouble();
        out["size"] = sizeGb > 0 ? QString::number(sizeGb, 'f', 2) + " ГБ" : "?";
        out["seeders"] = t.value("seeders");
        out["tracker"] = "AniLibria";
        seenMagnets.insert(magnet);
        combined << out;
    }

    for (const QVariant &tv : m_jacredTorrents) {
        const QString magnet = tv.toMap().value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        seenMagnets.insert(magnet);
        combined << tv;
    }

    for (const QVariant &tv : m_anistarTorrents) {
        const QString magnet = tv.toMap().value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        seenMagnets.insert(magnet);
        combined << tv;
    }

    const QString cacheId = m_item.value("id").toString();
    if (!cacheId.isEmpty())
        g_torrentSearchCache[cacheId] = combined;

    emit torrentsLoading(false);
    emit torrentsReady(combined);
}

void DetailBridge::searchTorrents() {
    const QString cacheId = m_item.value("id").toString();
    if (!cacheId.isEmpty() && g_torrentSearchCache.contains(cacheId)) {
        m_jacredSearchDone = true;
        emit torrentsLoading(false);
        emit torrentsReady(g_torrentSearchCache.value(cacheId));
        return;
    }

    const bool hentai = isHentaiItem(m_item);
    const QStringList queries = hentai ? hentaiTorrentQueries(m_item) : jacredQueriesFromItem(m_item);
    if (queries.isEmpty()) {
        m_jacredTorrents.clear();
        m_jacredSearchDone = true;
        if (hentai)
            m_deferredTorrentSearch = true;
        emit torrentsLoading(false);
        emit torrentsReady(QVariantList());
        return;
    }

    ++m_torrentSearchGen;
    const int searchGen = m_torrentSearchGen;
    const int minScore = hentai ? kTorrentMinScoreHentai : kTorrentMinScoreDefault;

    emit torrentsLoading(true);
    m_jacredSearchDone = false;
    m_jacredTorrents.clear();

    const int year = m_item.value("year").toInt();
    const QString kind = m_item.value("kind").toString();
    const QVariantMap itemSnapshot = m_item;
    auto accumulated = std::make_shared<QVariantList>();

    // Сводка по всему поиску (не по одному варианту) — чтобы на практике
    // увидеть, не растягивается ли worst-case на нестандартных названиях
    // из-за backoff'а на 429 (см. обсуждение: 3 повтора одного варианта —
    // уже 2+4+8с, а если таких вариантов несколько подряд — может набежать
    // намного больше, чем было до этого изменения).
    auto searchClock = std::make_shared<QElapsedTimer>();
    searchClock->start();
    auto total429 = std::make_shared<int>(0);

    QPointer<DetailBridge> self(this);
    auto finishSearch = [self, searchGen, accumulated, minScore, searchClock, total429, title = m_item.value("title").toString()]() {
        qInfo("DetailBridge: поиск торрентов для \"%s\" завершён за %lldмс, 429 поймано: %d",
              qUtf8Printable(title), searchClock->elapsed(), *total429);
        if (!self || searchGen != self->m_torrentSearchGen)
            return;
        self->m_jacredTorrents = rankTorrentResults(*accumulated, self->m_item, minScore);
        self->m_jacredSearchDone = true;
        // Отложенный re-search (после обогащения названий) нужен ТОЛЬКО если
        // текущий поиск не нашёл ничего. Раньше при наличии результатов они
        // выбрасывались и поиск гонялся второй раз целиком — лишняя задержка
        // и лишние запросы к JacRed/Sukebei (риск 429).
        if (self->m_deferredTorrentSearch && self->m_jacredTorrents.isEmpty()) {
            self->m_deferredTorrentSearch = false;
            self->searchTorrents();
            return;
        }
        self->m_deferredTorrentSearch = false;
        self->emitMergedTorrents();
    };

    // retry429 — счётчик повторов ТЕКУЩЕГО варианта названия именно из-за 429
    // (rate limit), отдельно от обычного перехода к следующему варианту.
    // При 429 пере-запрашиваем тот же вариант с растущей паузой (backoff),
    // вместо того чтобы тратить его молча как "0 результатов" и переходить
    // дальше — иначе на всплеске нагрузки (несколько параллельных поисков)
    // легко потерять реально существующие раздачи только из-за бана.
    auto runQueryAt = std::make_shared<std::function<void(int, int)>>();
    *runQueryAt = [self, queries, runQueryAt, hentai, year, kind, searchGen, itemSnapshot, accumulated,
                   finishSearch, minScore, total429](int index, int retry429) {
        if (!self || searchGen != self->m_torrentSearchGen)
            return;
        if (index >= queries.size()) {
            finishSearch();
            return;
        }

        const auto onResults = [self, runQueryAt, index, retry429, searchGen, itemSnapshot, accumulated,
                                finishSearch, hentai, year, kind, minScore, queries, total429]
                               (QVariantList results, QString err, int httpStatus) {
            Q_UNUSED(err);
            if (!self || searchGen != self->m_torrentSearchGen)
                return;

            constexpr int kMax429Retries = 3;
            if (httpStatus == 429)
                ++(*total429);
            if (httpStatus == 429 && retry429 < kMax429Retries) {
                const int backoffMs = 2000 * (1 << retry429) + jacredJitterMs(0, 500);
                qWarning("JacRed/Sukebei: HTTP 429 (rate limit) для \"%s\" — backoff %dms, повтор %d/%d",
                         qUtf8Printable(queries.at(index)), backoffMs, retry429 + 1, kMax429Retries);
                QTimer::singleShot(backoffMs, [self, runQueryAt, index, retry429, searchGen]() {
                    if (!self || searchGen != self->m_torrentSearchGen)
                        return;
                    (*runQueryAt)(index, retry429 + 1); // тот же вариант, не следующий
                });
                return;
            }
            if (httpStatus == 429) {
                qWarning("JacRed/Sukebei: 429 после %d попыток для \"%s\" — пропускаем вариант",
                         kMax429Retries, qUtf8Printable(queries.at(index)));
            }

            if (hentai) {
                for (const QVariant &tv : results)
                    accumulated->append(tv);
            } else {
                const QVariantList filtered = filterJacredResults(results, year, kind);
                for (const QVariant &tv : filtered)
                    accumulated->append(tv);
            }

            const QVariantList ranked = rankTorrentResults(*accumulated, itemSnapshot, minScore);
            int strongMatches = 0;
            const int strongThreshold = hentai ? 20 : 45;
            for (const QVariant &tv : ranked) {
                if (scoreTorrentRelevance(tv.toMap().value(QStringLiteral("title")).toString(), itemSnapshot)
                    >= strongThreshold)
                    ++strongMatches;
            }
            // Если самый первый (обычно самый точный) запрос уже дал уверенное
            // совпадение — незачем ждать второй ради подстраховки: именно это
            // лишнее ожидание и создавало разницу с сайтом JacRed, где
            // пользователь делает ровно один запрос сам.
            if (strongMatches >= 1) {
                finishSearch();
                return;
            }
            // Пауза перед следующим запросом — см. комментарий у jacredJitterMs().
            QTimer::singleShot(jacredJitterMs(700, 1100), [self, runQueryAt, index, searchGen]() {
                if (!self || searchGen != self->m_torrentSearchGen)
                    return;
                (*runQueryAt)(index + 1, 0);
            });
        };

        if (hentai)
            self->m_sukebei.search(queries.at(index), onResults);
        else
            self->m_jacred.search(queries.at(index), onResults);
    };
    (*runQueryAt)(0, 0);
}

void DetailBridge::selectTorrent(const QString &magnet) {
    m_lastTorrentMagnet = magnet;
}

void DetailBridge::setStatus(const QString &status) {
    QString id = m_item.value("id").toString();
    if (id.isEmpty())
        return;
    if (status.isEmpty()) {
        StatusStore::instance()->removeStatus(id);
    } else {
        const QString poster = ShikimoriClient::bestPosterUrl(
            m_item.value("poster").toString(), m_item.value("posterHd").toString());
        StatusStore::instance()->setStatus(id, status, m_item.value("title").toString(), poster);
    }
    emit statusChanged();
}

void DetailBridge::play(int episode, const QString &translationId) {
    if (!m_playback) {
        emit error("Плеер не подключён");
        return;
    }
    QString id = m_item.value("id").toString();
    QString title = m_item.value("title").toString();
    QString poster = ShikimoriClient::bestPosterUrl(
        m_item.value("poster").toString(), m_item.value("posterHd").toString());

    // Каждый play() отменяет резолвы предыдущих — иначе быстрый клик
    // "серия 5 -> серия 3" может применить URL пятой серии после третьей
    // (getEpisodeStream асинхронный, порядок ответов не гарантирован).
    const int gen = ++m_playGen;

    if (translationId == "torrent") {
        if (m_lastTorrentMagnet.isEmpty()) {
            emit error("Раздача не выбрана — выбери торрент в списке");
            return;
        }
        StatusStore::instance()->setTorrent(id, QString(), m_lastTorrentMagnet, title, poster);
        m_playback->playTorrentEpisode(m_lastTorrentMagnet, episode, QStringLiteral("torrent"));
    } else if (translationId == "anilibria") {
        if (m_anilibriaRelease.isEmpty()) {
            emit error("Этот тайтл не найден в каталоге AniLibria API");
            return;
        }
        QString url = AniLibriaClient::getEpisodeStream(m_anilibriaRelease, episode);
        if (url.isEmpty()) {
            emit error(QString("Серия %1 недоступна в AniLibria API").arg(episode));
            return;
        }
        m_playback->playDirectUrl(url, episode, false, QStringLiteral("anilibria"));
    } else if (translationId.startsWith(QStringLiteral("cvh_"))) {
        if (m_animegoId.isEmpty()) {
            emit error(QStringLiteral("Озвучки CVH ещё не загружены"));
            return;
        }
        QPointer<DetailBridge> self(this);
        m_animego.getEpisodeStream(m_animegoId, episode, translationId,
                                   [self, episode, translationId, gen](QString url, QString err) {
                                       if (!self || gen != self->m_playGen)
                                           return;
                                       if (url.isEmpty()) {
                                           emit self->error(err.isEmpty()
                                                          ? QString("Не удалось получить ссылку на серию %1")
                                                                .arg(episode)
                                                          : err);
                                           return;
                                       }
                                       qInfo("DetailBridge: CVH stream ep=%d direct host=%s",
                                             episode, qUtf8Printable(QUrl(url).host()));
                                       // CVH CDN (okcdn) — напрямую; прокси нужен только для animego.org.
                                       if (self->m_playback)
                                           self->m_playback->playDirectUrl(url, episode, false, translationId);
                                   });
    } else if (translationId.startsWith(QStringLiteral("hentasis_"))) {
        QPointer<DetailBridge> self(this);
        m_hentasis.getEpisodeStream(translationId, episode,
                                    [self, episode, translationId, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        if (url.isEmpty()) {
                                            emit self->error(err.isEmpty()
                                                           ? QString("Серия %1 недоступна (Hentasis)").arg(episode)
                                                           : err);
                                            return;
                                        }
                                        qInfo("DetailBridge: Hentasis stream ep=%d host=%s",
                                              episode, qUtf8Printable(QUrl(url).host()));
                                        if (self->m_playback)
                                            self->m_playback->playDirectUrl(url, episode, false, translationId);
                                    });
    } else if (translationId.startsWith(QStringLiteral("anistar_"))) {
        QPointer<DetailBridge> self(this);
        m_anistar.getEpisodeStream(translationId, episode,
                                   [self, episode, translationId, gen](QString url, QString err) {
                                       if (!self || gen != self->m_playGen)
                                           return;
                                       if (url.isEmpty()) {
                                           emit self->error(err.isEmpty()
                                                          ? QString("Серия %1 недоступна (AniStar)").arg(episode)
                                                          : err);
                                           return;
                                       }
                                       qInfo("DetailBridge: AniStar stream ep=%d host=%s",
                                             episode, qUtf8Printable(QUrl(url).host()));
                                       if (self->m_playback)
                                           self->m_playback->playDirectUrl(url, episode, true, translationId);
                                   });
    } else if (translationId.startsWith(QStringLiteral("kodik_"))) {
        const QString rawId = translationId.mid(6); // strlen("kodik_")
        QPointer<DetailBridge> self(this);
        m_kodik.getEpisodeStream(id, episode, rawId,
                                 [self, episode, translationId, gen](QString url, QString err) {
                                     if (!self || gen != self->m_playGen)
                                         return;
                                     if (url.isEmpty()) {
                                         emit self->error(err.isEmpty()
                                                        ? QString("Не удалось получить ссылку на серию %1 (Kodik)")
                                                              .arg(episode)
                                                        : err);
                                         return;
                                     }
                                     qInfo("DetailBridge: Kodik stream ep=%d host=%s",
                                           episode, qUtf8Printable(QUrl(url).host()));
                                     // Kodik геоблокирован — в отличие от CVH/okcdn, поток
                                     // нужно качать через прокси (useProxy=true).
                                     if (self->m_playback)
                                         self->m_playback->playDirectUrl(url, episode, true, translationId);
                                 });
    } else if (translationId.startsWith(QStringLiteral("animetka_"))) {
        QString tid;
        QString quality;
        AnimetkaClient::parseTranslationId(translationId, &tid, &quality);
        QPointer<DetailBridge> self(this);
        m_animetka.getEpisodeStream(translationId, episode, quality,
                                    [self, id, episode, translationId, tid, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        // Прямой HLS / animetka video-proxy — без Kodik-прокси.
                                        if (!url.isEmpty() && !url.startsWith(QLatin1String("kodik_fallback:"))) {
                                            qInfo("DetailBridge: Animetka stream ep=%d host=%s",
                                                  episode, qUtf8Printable(QUrl(url).host()));
                                            if (self->m_playback)
                                                self->m_playback->playDirectUrl(url, episode, false, translationId);
                                            return;
                                        }
                                        if (!err.isEmpty()) {
                                            emit self->error(err);
                                            return;
                                        }
                                        // Fallback: тот же translation id в Kodik (часто совпадает)
                                        const QString kodikTid = url.startsWith(QLatin1String("kodik_fallback:"))
                                            ? url.mid(QStringLiteral("kodik_fallback:").size())
                                            : tid;
                                        qInfo("DetailBridge: Animetka → Kodik fallback tid=%s ep=%d",
                                              qUtf8Printable(kodikTid), episode);
                                        self->m_kodik.getEpisodeStream(
                                            id, episode, kodikTid,
                                            [self, episode, translationId, gen](QString kurl, QString kerr) {
                                                if (!self || gen != self->m_playGen)
                                                    return;
                                                if (kurl.isEmpty()) {
                                                    emit self->error(
                                                        kerr.isEmpty()
                                                            ? QStringLiteral("Animetka/Kodik: нет потока для серии %1")
                                                                  .arg(episode)
                                                            : kerr);
                                                    return;
                                                }
                                                if (self->m_playback)
                                                    self->m_playback->playDirectUrl(kurl, episode, true, translationId);
                                            });
                                    });
    } else {
        emit error(QStringLiteral("Неизвестный источник озвучки"));
    }
}

void DetailBridge::playSmashMixed(int episode, const QString &audioTranslationId) {
    if (!m_playback) {
        emit error("Плеер не подключён");
        return;
    }
    if (m_lastTorrentMagnet.isEmpty()) {
        emit error("Раздача не выбрана — выбери торрент в списке");
        return;
    }
    QString id = m_item.value("id").toString();
    QString title = m_item.value("title").toString();
    QString poster = ShikimoriClient::bestPosterUrl(
        m_item.value("poster").toString(), m_item.value("posterHd").toString());
    const int gen = ++m_playGen;
    StatusStore::instance()->setTorrent(id, QString(), m_lastTorrentMagnet, title, poster);
    m_playback->playTorrentEpisode(m_lastTorrentMagnet, episode, QStringLiteral("torrent"));
    // playTorrentEpisode() сбрасывает хинт — ставим заново, ПОСЛЕ вызова.
    m_playback->setSmashAudioHint(audioTranslationId);

    if (audioTranslationId.isEmpty()) {
        emit error(QStringLiteral("Смэш: озвучка не выбрана — видео играет без звука"));
        return;
    }

    QPointer<DetailBridge> self(this);
    if (audioTranslationId.startsWith(QStringLiteral("cvh_"))) {
        if (m_animegoId.isEmpty()) {
            emit error(QStringLiteral("Озвучки CVH ещё не загружены — видео идёт без Смэш-звука"));
            return;
        }
        m_animego.getEpisodeStream(m_animegoId, episode, audioTranslationId,
                                    [self, episode, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        if (url.isEmpty()) {
                                            emit self->error(err.isEmpty()
                                                ? QString("Смэш: не удалось получить озвучку CVH для серии %1 — видео без звука")
                                                      .arg(episode)
                                                : err);
                                            return;
                                        }
                                        if (self->m_playback)
                                            self->m_playback->attachExternalAudio(url, false);
                                    });
    } else if (audioTranslationId.startsWith(QStringLiteral("kodik_"))) {
        const QString rawId = audioTranslationId.mid(6); // strlen("kodik_")
        m_kodik.getEpisodeStream(id, episode, rawId,
                                  [self, episode, gen](QString url, QString err) {
                                      if (!self || gen != self->m_playGen)
                                          return;
                                      if (url.isEmpty()) {
                                          emit self->error(err.isEmpty()
                                              ? QString("Смэш: не удалось получить озвучку Kodik для серии %1 — видео без звука")
                                                    .arg(episode)
                                              : err);
                                          return;
                                      }
                                      // Kodik геоблокирован — звук тянем через прокси, как и
                                      // в обычном воспроизведении через Kodik.
                                      if (self->m_playback)
                                          self->m_playback->attachExternalAudio(url, true);
                                  });
    } else {
        emit error(QStringLiteral("Смэш: неизвестный источник озвучки «%1» — видео играет без звука")
                       .arg(audioTranslationId));
    }
}
