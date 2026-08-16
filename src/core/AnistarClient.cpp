#include "AnistarClient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace {

constexpr int kMinSearchScore = 12;
const QString kIdPrefix = QStringLiteral("anistar_");

QString dleSearchPath(const QString &query) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("do"), QStringLiteral("search"));
    q.addQueryItem(QStringLiteral("subaction"), QStringLiteral("search"));
    q.addQueryItem(QStringLiteral("story"), query);
    return QStringLiteral("/index.php?") + q.toString(QUrl::FullyEncoded);
}

QString fallbackSearchPath(const QString &query) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("s"), query);
    return QStringLiteral("/?") + q.toString(QUrl::FullyEncoded);
}

QString magnetFromTorrentBytes(const QByteArray &data) {
    static const QRegularExpression magnetRe(QStringLiteral(R"(magnet:\?[^"'\s<]+)"));
    const auto m = magnetRe.match(QString::fromUtf8(data));
    if (m.hasMatch())
        return m.captured(0);

    const int infoPos = data.indexOf("4:infod");
    if (infoPos < 0)
        return {};
    int depth = 0;
    int dictStart = -1;
    for (int i = infoPos; i < data.size(); ++i) {
        if (data.at(i) == 'd') {
            if (depth == 0)
                dictStart = i;
            ++depth;
        } else if (data.at(i) == 'e') {
            --depth;
            if (depth == 0 && dictStart >= 0) {
                const QByteArray infoDict = data.mid(dictStart, i - dictStart + 1);
                // info_hash = SHA1(bencoded info dict)
                // Qt doesn't have SHA1 in minimal include - use simple scan for btih in announce
                break;
            }
        }
    }
    static const QRegularExpression btihRe(
        QStringLiteral(R"(urn:btih:([0-9a-fA-F]{40}|[0-9a-zA-Z]{32}))"));
    const auto btih = btihRe.match(QString::fromUtf8(data));
    if (btih.hasMatch())
        return QStringLiteral("magnet:?xt=urn:btih:") + btih.captured(1);
    return {};
}

QVariantList parseTorrentLinks(const QString &html, const QString &mirrorBase) {
    static const QRegularExpression linkRe(
        QStringLiteral(R"re(/engine/gettorrent\.php\?id=(\d+)[^"']*"[^>]*>([^<]+))re"),
        QRegularExpression::CaseInsensitiveOption);
    QVariantList out;
    QSet<QString> seen;
    auto it = linkRe.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString id = m.captured(1);
        if (seen.contains(id))
            continue;
        seen.insert(id);
        QString label = HentaiSiteCommon::decodeHtmlEntities(m.captured(2)).trimmed();
        if (label.isEmpty())
            label = QStringLiteral("AniStar #%1").arg(id);
        QVariantMap row;
        row[QStringLiteral("title")] = label;
        row[QStringLiteral("magnet")] = QString();
        row[QStringLiteral("torrentUrl")] = mirrorBase + QStringLiteral("/engine/gettorrent.php?id=") + id;
        row[QStringLiteral("size")] = QStringLiteral("?");
        row[QStringLiteral("seeders")] = 0;
        row[QStringLiteral("tracker")] = QStringLiteral("AniStar");
        out << row;
    }
    return out;
}

} // namespace

QStringList AnistarClient::defaultMirrors() const {
    return {
        QStringLiteral("https://v28.astar.bz"),
        QStringLiteral("https://v20.astar.bz"),
        QStringLiteral("https://v8.astar.bz"),
        QStringLiteral("https://v6.astar.bz"),
        QStringLiteral("https://anistar.org"),
        QStringLiteral("https://www.anistar.org"),
    };
}

QStringList AnistarClient::mirrorSeeds() const {
    return {
        QStringLiteral("https://v28.astar.bz/"),
        QStringLiteral("https://v20.astar.bz/"),
        QStringLiteral("https://anistar.org/"),
        QStringLiteral("https://anistar.org/as_act.php"),
    };
}

void AnistarClient::resolveMirrors(std::function<void(QStringList mirrors)> callback) {
    HentaiSiteCommon::discoverMirrors(mirrorSeeds(), defaultMirrors(), true, std::move(callback));
}

void AnistarClient::resolvePage(
    const QStringList &queries, int year,
    std::function<void(QString pagePath, QString mirrorBase, QString err)> callback) {
    if (queries.isEmpty()) {
        callback({}, {}, QStringLiteral("Пустой запрос для AniStar"));
        return;
    }

    QPointer<AnistarClient> self(this);

    auto startSearch = [self, callback, queries, year](const QStringList &mirrors) {
        if (!self) {
            callback({}, {}, QString());
            return;
        }
        self->m_mirrors = mirrors;

        auto trySearch = std::make_shared<std::function<void(int, int)>>();
        *trySearch = [self, callback, queries, year, mirrors, trySearch](int queryIndex, int variant) {
            if (!self) {
                callback({}, {}, QString());
                return;
            }
            if (queryIndex >= queries.size()) {
                callback({}, {}, QString());
                return;
            }

            const QString query = queries.at(queryIndex);
            const QString path = variant == 0 ? dleSearchPath(query) : fallbackSearchPath(query);

            HentaiSiteCommon::fetchFirstMirror(
                mirrors, path, true,
                [self, callback, queries, year, mirrors, queryIndex, variant, trySearch](QByteArray body,
                                                                                             QString mirrorBase,
                                                                                             QString err) {
                    if (!self) {
                        callback({}, {}, QString());
                        return;
                    }
                    if (!err.isEmpty()) {
                        if (variant == 0)
                            (*trySearch)(queryIndex, 1);
                        else
                            (*trySearch)(queryIndex + 1, 0);
                        return;
                    }
                    self->m_activeMirror = mirrorBase;
                    self->m_mirrors = HentaiSiteCommon::mergeMirrorLists({mirrorBase}, mirrors);

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
                            (*trySearch)(queryIndex, 1);
                        else
                            (*trySearch)(queryIndex + 1, 0);
                        return;
                    }

                    const QUrl pageUrl(best.pageUrl);
                    const QString pagePath = pageUrl.path()
                        + (pageUrl.hasQuery() ? QLatin1Char('?') + pageUrl.query(QUrl::FullyEncoded)
                                              : QString());
                    self->m_lastPagePath = pagePath;
                    callback(pagePath, mirrorBase, QString());
                });
        };
        (*trySearch)(0, 0);
    };

    if (!m_mirrors.isEmpty()) {
        startSearch(m_mirrors);
        return;
    }

    resolveMirrors([self, callback, startSearch](QStringList mirrors) {
        if (!self) {
            callback({}, {}, QString());
            return;
        }
        if (mirrors.isEmpty()) {
            callback({}, {}, QStringLiteral("Зеркала AniStar недоступны"));
            return;
        }
        startSearch(mirrors);
    });
}

void AnistarClient::loadTranslations(const QString &title, const QString &originalTitle,
                                     const QString &englishTitle, const QString &japaneseTitle, int year,
                                     TranslationsCallback callback) {
    m_episodeUrls.clear();
    m_activeMirror.clear();
    m_lastPagePath.clear();
    m_mirrors.clear();

    const QStringList queries =
        HentaiSiteCommon::buildSearchQueries(title, originalTitle, englishTitle, japaneseTitle);

    QPointer<AnistarClient> self(this);
    resolvePage(queries, year, [self, callback](QString pagePath, QString mirrorBase, QString err) {
        if (!self) {
            callback({}, QString());
            return;
        }
        if (!err.isEmpty() || pagePath.isEmpty()) {
            callback({}, err);
            return;
        }

        HentaiSiteCommon::fetchFirstMirror(
            self->m_mirrors, pagePath, true,
            [self, callback, mirrorBase](QByteArray pageBody, QString pageMirror, QString pageErr) {
                if (!self) {
                    callback({}, QString());
                    return;
                }
                if (!pageErr.isEmpty()) {
                    callback({}, pageErr);
                    return;
                }
                if (!pageMirror.isEmpty())
                    self->m_activeMirror = pageMirror;

                const QList<HentaiVoiceTrack> tracks =
                    HentaiSiteCommon::parseMp4VoiceTracks(HentaiSiteCommon::htmlBodyToString(pageBody));
                if (tracks.isEmpty()) {
                    callback({}, QString());
                    return;
                }

                for (const HentaiVoiceTrack &track : tracks) {
                    const QString tid = kIdPrefix + track.id;
                    self->m_episodeUrls[tid] = track.episodeUrls;
                }
                callback(HentaiSiteCommon::tracksToTranslations(tracks, kIdPrefix,
                                                                QStringLiteral("AniStar · ")),
                         QString());
            });
    });
}

void AnistarClient::fetchPageTorrents(const QString &title, const QString &originalTitle,
                                      const QString &englishTitle, const QString &japaneseTitle, int year,
                                      TorrentsCallback callback) {
    const QStringList queries =
        HentaiSiteCommon::buildSearchQueries(title, originalTitle, englishTitle, japaneseTitle);

    QPointer<AnistarClient> self(this);
    auto finishWithPage = [self, callback](const QByteArray &pageBody, const QString &mirrorBase) {
        if (!self)
            return;
        QVariantList torrents =
            parseTorrentLinks(HentaiSiteCommon::htmlBodyToString(pageBody), mirrorBase);
        if (torrents.isEmpty()) {
            callback({}, QString());
            return;
        }

        auto pending = std::make_shared<int>(torrents.size());
        auto out = std::make_shared<QVariantList>(torrents);
        for (int i = 0; i < torrents.size(); ++i) {
            const QString torrentUrl = torrents.at(i).toMap().value(QStringLiteral("torrentUrl")).toString();
            if (torrentUrl.isEmpty()) {
                if (--(*pending) == 0)
                    callback(*out, QString());
                continue;
            }
            QNetworkRequest netRequest{QUrl(torrentUrl)};
            netRequest.setHeader(QNetworkRequest::UserAgentHeader,
                                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));
            QNetworkReply *reply = NetworkManager::instance()->get(netRequest);
            QObject::connect(reply, &QNetworkReply::finished, reply,
                             [reply, out, pending, i, callback]() {
                                 QByteArray data = reply->readAll();
                                 const QString magnet = magnetFromTorrentBytes(data);
                                 reply->deleteLater();
                                 if (!magnet.isEmpty()) {
                                     QVariantMap row = out->at(i).toMap();
                                     row[QStringLiteral("magnet")] = magnet;
                                     (*out)[i] = row;
                                 }
                                 if (--(*pending) == 0)
                                     callback(*out, QString());
                             });
        }
    };

    if (!m_lastPagePath.isEmpty() && !m_activeMirror.isEmpty()) {
        HentaiSiteCommon::fetchFirstMirror(
            m_mirrors, m_lastPagePath, true,
            [finishWithPage, callback](QByteArray body, QString mirror, QString err) {
                if (!err.isEmpty()) {
                    callback({}, err);
                    return;
                }
                finishWithPage(body, mirror);
            });
        return;
    }

    resolvePage(queries, year, [self, finishWithPage, callback](QString pagePath, QString, QString err) {
        if (!self || !err.isEmpty() || pagePath.isEmpty()) {
            callback({}, err);
            return;
        }
        HentaiSiteCommon::fetchFirstMirror(
            self->m_mirrors, pagePath, true,
            [finishWithPage, callback](QByteArray body, QString mirror, QString pageErr) {
                if (!pageErr.isEmpty()) {
                    callback({}, pageErr);
                    return;
                }
                finishWithPage(body, mirror);
            });
    });
}

void AnistarClient::getEpisodeStream(const QString &translationId, int episode, StreamCallback callback) {
    const QString url = m_episodeUrls.value(translationId).value(episode);
    if (url.isEmpty()) {
        callback({}, QStringLiteral("Серия недоступна на AniStar"));
        return;
    }
    callback(url, QString());
}