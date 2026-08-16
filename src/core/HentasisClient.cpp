#include "HentasisClient.h"

#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kMinSearchScore = 12;
const QString kIdPrefix = QStringLiteral("hentasis_");

QString searchPath(const QString &query, int variant) {
    if (variant == 1) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("do"), QStringLiteral("search"));
        q.addQueryItem(QStringLiteral("subaction"), QStringLiteral("search"));
        q.addQueryItem(QStringLiteral("story"), query);
        return QStringLiteral("/index.php?") + q.toString(QUrl::FullyEncoded);
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("s"), query);
    return QStringLiteral("/?") + q.toString(QUrl::FullyEncoded);
}

} // namespace

QStringList HentasisClient::defaultMirrors() const {
    return {
        QStringLiteral("http://hentasis1.top"),
        QStringLiteral("https://hentasis1.top"),
        QStringLiteral("http://hentasis2.top"),
        QStringLiteral("https://hentasis2.top"),
        QStringLiteral("https://v3.hentasis5.top"),
        QStringLiteral("https://v1.hentasis.me"),
    };
}

QStringList HentasisClient::mirrorSeeds() const {
    return {
        QStringLiteral("http://hentasis1.top/"),
        QStringLiteral("http://hentasis2.top/"),
        QStringLiteral("https://v1.hentasis.me/"),
    };
}

void HentasisClient::resolveMirrors(std::function<void(QStringList mirrors)> callback) {
    HentaiSiteCommon::discoverMirrors(mirrorSeeds(), defaultMirrors(), false, std::move(callback));
}

void HentasisClient::loadTranslations(const QString &title, const QString &originalTitle,
                                      const QString &englishTitle, const QString &japaneseTitle,
                                      int year, TranslationsCallback callback) {
    m_episodeUrls.clear();
    m_activeMirror.clear();
    m_mirrors.clear();

    const QStringList queries =
        HentaiSiteCommon::buildSearchQueries(title, originalTitle, englishTitle, japaneseTitle);
    if (queries.isEmpty()) {
        callback({}, QStringLiteral("Пустой запрос для Hentasis"));
        return;
    }

    QPointer<HentasisClient> self(this);
    resolveMirrors([self, callback, queries, year](QStringList mirrors) {
        if (!self) {
            callback({}, QString());
            return;
        }
        if (mirrors.isEmpty()) {
            callback({}, QStringLiteral("Зеркала Hentasis недоступны"));
            return;
        }
        self->m_mirrors = std::move(mirrors);

        auto tryQuery = std::make_shared<std::function<void(int, int)>>();
        *tryQuery = [self, callback, queries, year, tryQuery](int queryIndex, int variant) {
            if (!self) {
                callback({}, QString());
                return;
            }
            if (queryIndex >= queries.size()) {
                callback({}, QString());
                return;
            }

            const QString query = queries.at(queryIndex);
            HentaiSiteCommon::fetchFirstMirror(
                self->m_mirrors, searchPath(query, variant), false,
                [self, callback, queries, year, queryIndex, variant, tryQuery](QByteArray body,
                                                                                 QString mirrorBase,
                                                                                 QString err) {
                    if (!self) {
                        callback({}, QString());
                        return;
                    }
                    if (!err.isEmpty()) {
                        if (variant == 0)
                            (*tryQuery)(queryIndex, 1);
                        else
                            (*tryQuery)(queryIndex + 1, 0);
                        return;
                    }
                    self->m_activeMirror = mirrorBase;
                    self->m_mirrors = HentaiSiteCommon::mergeMirrorLists({mirrorBase}, self->m_mirrors);

                    QList<HentaiSearchHit> hits = HentaiSiteCommon::parseDleSearchHits(body, mirrorBase);
                    int bestScore = 0;
                    HentaiSearchHit best;
                    for (HentaiSearchHit &hit : hits) {
                        hit.score = HentaiSiteCommon::scoreSearchHit(hit, queries, year);
                        if (hit.score > bestScore) {
                            bestScore = hit.score;
                            best = hit;
                        }
                    }
                    if (bestScore < kMinSearchScore || best.pageUrl.isEmpty()) {
                        if (variant == 0)
                            (*tryQuery)(queryIndex, 1);
                        else
                            (*tryQuery)(queryIndex + 1, 0);
                        return;
                    }

                    const QUrl pageUrl(best.pageUrl);
                    const QString pagePath = pageUrl.path()
                        + (pageUrl.hasQuery() ? QLatin1Char('?') + pageUrl.query(QUrl::FullyEncoded)
                                              : QString());

                    HentaiSiteCommon::fetchFirstMirror(
                        self->m_mirrors, pagePath, false,
                        [self, callback, tryQuery, queryIndex, variant](QByteArray pageBody,
                                                                          QString pageMirror,
                                                                          QString pageErr) {
                            if (!self) {
                                callback({}, QString());
                                return;
                            }
                            if (!pageErr.isEmpty()) {
                                if (variant == 0)
                                    (*tryQuery)(queryIndex, 1);
                                else
                                    (*tryQuery)(queryIndex + 1, 0);
                                return;
                            }
                            if (!pageMirror.isEmpty()) {
                                self->m_activeMirror = pageMirror;
                                self->m_mirrors =
                                    HentaiSiteCommon::mergeMirrorLists({pageMirror}, self->m_mirrors);
                            }

                            const QList<HentaiVoiceTrack> tracks =
                                HentaiSiteCommon::parseMp4VoiceTracks(HentaiSiteCommon::htmlBodyToString(pageBody));
                            if (tracks.isEmpty()) {
                                if (variant == 0)
                                    (*tryQuery)(queryIndex, 1);
                                else
                                    (*tryQuery)(queryIndex + 1, 0);
                                return;
                            }

                            for (const HentaiVoiceTrack &track : tracks) {
                                const QString tid = kIdPrefix + track.id;
                                self->m_episodeUrls[tid] = track.episodeUrls;
                            }
                            callback(HentaiSiteCommon::tracksToTranslations(tracks, kIdPrefix,
                                                                            QStringLiteral("Hentasis · ")),
                                     QString());
                        });
                });
        };
        (*tryQuery)(0, 0);
    });
}

void HentasisClient::getEpisodeStream(const QString &translationId, int episode,
                                      StreamCallback callback) {
    const QString url = m_episodeUrls.value(translationId).value(episode);
    if (url.isEmpty()) {
        callback({}, QStringLiteral("Серия недоступна на Hentasis"));
        return;
    }
    callback(url, QString());
}