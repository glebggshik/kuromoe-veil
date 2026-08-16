#include "AnimegoClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <memory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace {

const QString kAnimegoBase = QStringLiteral("https://animego.org");
const QString kCvhApiBase = QStringLiteral("https://plapi.cdnvideohub.com/api/v1/player/sv");
const QString kCvhPub = QStringLiteral("747");
const QString kCvhAggr = QStringLiteral("mali");
const QString kTranslationPrefix = QStringLiteral("cvh_");

using StringCallback = std::function<void(QString result, QString error)>;

constexpr int kMinSearchScore = 35;
constexpr int kMaxSearchCandidates = 6;

QString extractPlayerHtml(const QString &body) {
    const QString trimmed = body.trimmed();
    if (!trimmed.startsWith(QLatin1Char('{')))
        return body;
    const QJsonObject root = QJsonDocument::fromJson(trimmed.toUtf8()).object();
    return root.value(QStringLiteral("data")).toObject().value(QStringLiteral("content")).toString();
}

QString normalizeSearchText(QString text) {
    text = text.trimmed().toLower();
    text.replace(QRegularExpression(QStringLiteral(R"([\s\-–—_/]+)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(R"([:,.!?«»"'\(\)\[\]])")), QString());
    text.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    return text.trimmed();
}

QStringList searchTokens(const QString &text) {
    const QString norm = normalizeSearchText(text);
    if (norm.isEmpty())
        return {};
    return norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int wordOverlapScore(const QString &a, const QString &b) {
    const QStringList aw = searchTokens(a);
    const QStringList bw = searchTokens(b);
    if (aw.isEmpty() || bw.isEmpty())
        return 0;
    int hits = 0;
    for (const QString &w : aw) {
        if (w.size() < 3)
            continue;
        for (const QString &v : bw) {
            if (w == v || v.contains(w) || w.contains(v)) {
                ++hits;
                break;
            }
        }
    }
    return hits * 12;
}

int scoreField(const QString &field, const QStringList &wanted) {
    const QString norm = normalizeSearchText(field);
    if (norm.isEmpty())
        return 0;
    int best = 0;
    for (const QString &w : wanted) {
        const QString wn = normalizeSearchText(w);
        if (wn.isEmpty())
            continue;
        if (norm == wn) {
            best = qMax(best, 100);
            continue;
        }
        if (norm.contains(wn) || wn.contains(norm)) {
            best = qMax(best, 75);
            continue;
        }
        best = qMax(best, wordOverlapScore(norm, wn));
    }
    return best;
}

QString attrValue(const QString &attrs, const QString &name) {
    const QString dq = name + QStringLiteral("=\"");
    int pos = attrs.indexOf(dq);
    if (pos >= 0) {
        pos += dq.size();
        const int end = attrs.indexOf(QLatin1Char('"'), pos);
        if (end > pos)
            return attrs.mid(pos, end - pos);
    }
    const QString sq = name + QStringLiteral("='");
    pos = attrs.indexOf(sq);
    if (pos >= 0) {
        pos += sq.size();
        const int end = attrs.indexOf(QLatin1Char('\''), pos);
        if (end > pos)
            return attrs.mid(pos, end - pos);
    }
    return {};
}

QString animegoSlugIdFromHref(QString href) {
    href = href.trimmed();
    if (href.startsWith(QLatin1Char('/')))
        href = href.mid(1);
    if (href.startsWith(QStringLiteral("anime/")))
        href = href.mid(6);
    const int slash = href.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        href = href.mid(slash + 1);
    return href;
}

bool parseAnimegoSlugId(const QString &slugId, QString *slugOut, QString *idOut) {
    const int dash = slugId.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0 || dash >= slugId.size() - 1)
        return false;
    const QString tail = slugId.mid(dash + 1);
    for (const QChar ch : tail) {
        if (!ch.isDigit())
            return false;
    }
    if (slugOut)
        *slugOut = slugId.left(dash);
    if (idOut)
        *idOut = tail;
    return true;
}

QString extractOriginalTitle(const QString &chunk) {
    const int bodyPos = chunk.indexOf(QStringLiteral("ani-grid__item-body"));
    if (bodyPos < 0)
        return {};
    const int lighterPos = chunk.indexOf(QStringLiteral("fw-lighter"), bodyPos);
    if (lighterPos < 0)
        return {};
    const int gt = chunk.indexOf(QLatin1Char('>'), lighterPos);
    if (gt < 0)
        return {};
    const int endDiv = chunk.indexOf(QStringLiteral("</div>"), gt + 1);
    if (endDiv <= gt)
        return {};
    return chunk.mid(gt + 1, endDiv - gt - 1).trimmed();
}

int extractYear(const QString &chunk) {
    const int genresPos = chunk.indexOf(QStringLiteral("ani-grid__item-genres__link"));
    if (genresPos < 0)
        return 0;
    const int end = qMin(genresPos + 120, chunk.size());
    const QString slice = chunk.mid(genresPos, end - genresPos);
    for (int i = 0; i + 4 <= slice.size(); ++i) {
        if (slice.at(i).isDigit() && slice.at(i + 1).isDigit() && slice.at(i + 2).isDigit()
            && slice.at(i + 3).isDigit()) {
            const int year = slice.mid(i, 4).toInt();
            if (year >= 1960 && year <= 2100)
                return year;
        }
    }
    return 0;
}

bool extractSearchLink(const QString &chunk, QString *hrefOut, QString *titleOut) {
    const int titlePos = chunk.indexOf(QStringLiteral("ani-grid__item-title"));
    if (titlePos < 0)
        return false;

    const int aPos = chunk.indexOf(QStringLiteral("<a"), titlePos);
    if (aPos < 0 || aPos > titlePos + 400)
        return false;
    const int aEnd = chunk.indexOf(QLatin1Char('>'), aPos);
    if (aEnd < 0)
        return false;
    const QString attrs = chunk.mid(aPos, aEnd - aPos);

    QString href = animegoSlugIdFromHref(attrValue(attrs, QStringLiteral("href")));
    QString title = attrValue(attrs, QStringLiteral("title"));
    if (title.isEmpty()) {
        const int closeA = chunk.indexOf(QStringLiteral("</a>"), aEnd);
        if (closeA > aEnd)
            title = chunk.mid(aEnd + 1, closeA - aEnd - 1).trimmed();
    }
    if (href.isEmpty())
        return false;
    if (hrefOut)
        *hrefOut = href;
    if (titleOut)
        *titleOut = title.trimmed();
    return true;
}

QString normalizeEmbed(const QString &embed) {
    if (embed.startsWith(QStringLiteral("//")))
        return QStringLiteral("https:") + embed;
    return embed;
}

QString extractCvhId(const QString &embed) {
    const int marker = embed.indexOf(QStringLiteral("cdn-iframe/"));
    if (marker < 0)
        return {};
    const int start = marker + 11;
    const int end = embed.indexOf(QLatin1Char('/'), start);
    if (end <= start)
        return {};
    return embed.mid(start, end - start);
}

QString matchCvhStudio(const QString &label, const QStringList &studios) {
    const QString lo = label.toLower();
    for (const QString &s : studios) {
        if (s.toLower() == lo)
            return s;
    }
    for (const QString &s : studios) {
        const QString sl = s.toLower();
        if (lo.contains(sl) || sl.contains(lo))
            return s;
    }
    return {};
}

enum class AnimegoRequestKind { SearchHtml, PlayerHtml, JsonApi };

// Случайная пауза перед следующим запросом в цепочке (перебор кандидатов CVH,
// ретраи) — без неё запросы идут точно встык, секунда в секунду, что для
// антибота отличает скрипт от браузера сильнее, чем сам факт автоматизации.
int jitterMs(int minMs, int maxMs) {
    return QRandomGenerator::global()->bounded(minMs, maxMs + 1);
}

void tuneAnimegoRequest(QNetworkRequest &req, AnimegoRequestKind kind) {
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(45000);
    req.setRawHeader("Referer", "https://animego.org/");
    req.setRawHeader("sec-ch-ua", R"("Not_A Brand";v="8", "Chromium";v="120", "Google Chrome";v="120")");
    req.setRawHeader("sec-ch-ua-mobile", "?0");
    req.setRawHeader("sec-ch-ua-platform", "\"Windows\"");
    req.setRawHeader("Sec-Fetch-Site", "same-origin");
    req.setRawHeader("Sec-Fetch-Mode", kind == AnimegoRequestKind::JsonApi || kind == AnimegoRequestKind::PlayerHtml
                                            ? "cors" : "navigate");
    req.setRawHeader("Sec-Fetch-Dest", kind == AnimegoRequestKind::SearchHtml ? "document" : "empty");
    if (kind == AnimegoRequestKind::JsonApi) {
        req.setRawHeader("Accept", "application/json");
    } else if (kind == AnimegoRequestKind::PlayerHtml) {
        req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
        req.setRawHeader("X-Requested-With", "XMLHttpRequest");
    } else {
        req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    }
}

void httpGet(const QUrl &url, StringCallback callback, AnimegoRequestKind kind = AnimegoRequestKind::SearchHtml,
             int retriesLeft = 3, bool useLocal = false) {
    QNetworkRequest req(url);
    tuneAnimegoRequest(req, kind);
    NetworkManager *nm = NetworkManager::instance();
    QNetworkReply *reply = useLocal ? nm->getLocal(req) : nm->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, url, kind, retriesLeft, useLocal]() {
                         const QByteArray body = reply->readAll();
                         const auto err = reply->error();
                         const QString errText = reply->errorString();
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         reply->deleteLater();

                         const bool retryable = retriesLeft > 0
                             && (status == 502 || status == 503 || status == 504
                                 || err == QNetworkReply::TimeoutError);
                         if (retryable) {
                             QTimer::singleShot(1500 + jitterMs(0, 600), [url, callback, kind, retriesLeft, useLocal]() {
                                 httpGet(url, callback, kind, retriesLeft - 1, useLocal);
                             });
                             return;
                         }

                         if (err != QNetworkReply::NoError) {
                             callback({}, errText);
                             return;
                         }
                         if (status == 403 || status == 503
                             || QString::fromUtf8(body).contains(QStringLiteral("Cloudflare"),
                                                                 Qt::CaseInsensitive)) {
                             callback({},
                                      QStringLiteral(
                                          "AnimeGO недоступен (Cloudflare %1) — включи прокси в настройках")
                                          .arg(status));
                             return;
                         }
                         if (status >= 400) {
                             callback({}, QStringLiteral("HTTP %1").arg(status));
                             return;
                         }
                         callback(QString::fromUtf8(body), {});
                     });
}

bool appendSearchRow(QVariantList *out, QSet<QString> *seenIds, const QString &chunk, const QString &hrefIn,
                     const QString &titleIn) {
    const QString slugId = animegoSlugIdFromHref(hrefIn);
    if (slugId.isEmpty())
        return false;

    QString slug;
    QString animegoId;
    if (!parseAnimegoSlugId(slugId, &slug, &animegoId))
        return false;
    if (seenIds->contains(animegoId))
        return false;
    seenIds->insert(animegoId);

    QString title = titleIn.trimmed();
    if (title.isEmpty()) {
        title = slug;
        title.replace(QLatin1Char('-'), QLatin1Char(' '));
    }

    QVariantMap row;
    row[QStringLiteral("id")] = animegoId;
    row[QStringLiteral("slug")] = slug;
    row[QStringLiteral("link")] = kAnimegoBase + QStringLiteral("/anime/") + slugId;
    row[QStringLiteral("title")] = title;
    const QString originalTitle = extractOriginalTitle(chunk);
    if (!originalTitle.isEmpty())
        row[QStringLiteral("original_title")] = originalTitle;
    const int year = extractYear(chunk);
    if (year > 0)
        row[QStringLiteral("year")] = year;
    out->append(row);
    return true;
}

QVariantList parseSearchResults(const QString &html) {
    QVariantList out;
    QSet<QString> seenIds;

    const QString blockMarker = QStringLiteral("<div class=\"ani-grid__item g-col");
    int blockStart = 0;
    while (true) {
        const int markerPos = html.indexOf(blockMarker, blockStart);
        if (markerPos < 0)
            break;
        const int nextMarker = html.indexOf(blockMarker, markerPos + blockMarker.size());
        const QString chunk = nextMarker < 0 ? html.mid(markerPos)
                                             : html.mid(markerPos, nextMarker - markerPos);
        blockStart = markerPos + blockMarker.size();

        QString href;
        QString title;
        if (extractSearchLink(chunk, &href, &title))
            appendSearchRow(&out, &seenIds, chunk, href, title);
    }

    if (out.isEmpty() && html.size() > 50000) {
        const bool hasGrid = html.contains(blockMarker);
        qWarning("Animego parse: 0 results from %d bytes (grid marker=%s)",
                 html.size(), hasGrid ? "yes" : "no");
    }
    return out;
}

QStringList buildSearchQueries(const QString &title, const QString &originalTitle,
                               const QString &englishTitle) {
    QStringList queries;
    QSet<QString> seen;
    const auto add = [&](const QString &q) {
        const QString trimmed = q.trimmed();
        if (trimmed.isEmpty())
            return;
        const QString key = trimmed.toLower();
        if (seen.contains(key))
            return;
        seen.insert(key);
        queries.append(trimmed);
    };
    add(englishTitle);
    add(originalTitle);
    add(title);
    if (!title.isEmpty() && !englishTitle.isEmpty())
        add(title + QLatin1Char(' ') + englishTitle);

    const int colon = title.indexOf(QLatin1Char(':'));
    if (colon > 0 && colon < title.size() - 1)
        add(title.mid(colon + 1).trimmed());

    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!words.isEmpty()) {
        const QString lastWord = words.last();
        if (lastWord.size() >= 5)
            add(lastWord);
    }
    return queries;
}

QVariantList rankSearchResults(const QVariantList &results, const QStringList &wantedTitles, int year) {
    struct ScoredRow {
        int score = 0;
        QVariantMap row;
    };
    QList<ScoredRow> scored;
    scored.reserve(results.size());

    for (const QVariant &rv : results) {
        const QVariantMap r = rv.toMap();
        int score = scoreField(r.value(QStringLiteral("title")).toString(), wantedTitles);
        score = qMax(score, scoreField(r.value(QStringLiteral("original_title")).toString(), wantedTitles));
        const QString slug = r.value(QStringLiteral("slug")).toString().replace(QLatin1Char('-'), QLatin1Char(' '));
        score = qMax(score, scoreField(slug, wantedTitles));
        const int ry = r.value(QStringLiteral("year")).toInt();
        if (year > 0 && ry > 0 && ry != year)
            score -= 25;
        if (score < kMinSearchScore)
            continue;
        scored.append({score, r});
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredRow &a, const ScoredRow &b) {
        return a.score > b.score;
    });

    QVariantList out;
    QSet<QString> seenIds;
    for (const ScoredRow &sr : scored) {
        const QString id = sr.row.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || seenIds.contains(id))
            continue;
        seenIds.insert(id);
        QVariantMap row = sr.row;
        row[QStringLiteral("_score")] = sr.score;
        out << row;
        if (out.size() >= kMaxSearchCandidates)
            break;
    }
    return out;
}

struct CvhVoice {
    QString translationId;
    QString label;
    QString cvhId;
};

QVariantList parseCvhVoices(const QString &html, QList<CvhVoice> *voicesOut, int *totalEpisodes) {
    QVariantList list;
    voicesOut->clear();
    if (totalEpisodes)
        *totalEpisodes = 0;

    int providerPos = html.indexOf(QStringLiteral("id=\"provider\""));
    if (providerPos < 0)
        providerPos = html.indexOf(QStringLiteral("id='provider'"));
    if (providerPos < 0)
        return list;

    static const QRegularExpression btnRe(QStringLiteral("<button\\s+([^>]+)>"));
    auto it = btnRe.globalMatch(html, providerPos);
    int maxEp = 0;
    while (it.hasNext()) {
        const QString attrs = it.next().captured(1);
        const QString provider = attrValue(attrs, QStringLiteral("data-provider-title"));
        if (provider.compare(QStringLiteral("CVH"), Qt::CaseInsensitive) != 0)
            continue;

        const QString translationId = attrValue(attrs, QStringLiteral("data-ptranslation"));
        QString label = attrValue(attrs, QStringLiteral("data-translation-title"));
        label.replace(QStringLiteral(" (ошибка)"), QString());
        label = label.trimmed();
        const QString embed = normalizeEmbed(attrValue(attrs, QStringLiteral("data-player")));
        const QString cvhId = extractCvhId(embed);
        if (translationId.isEmpty() || label.isEmpty() || cvhId.isEmpty())
            continue;

        CvhVoice voice;
        voice.translationId = translationId;
        voice.label = label;
        voice.cvhId = cvhId;
        voicesOut->append(voice);

        QVariantMap row;
        row[QStringLiteral("id")] = kTranslationPrefix + translationId;
        row[QStringLiteral("name")] = label;
        row[QStringLiteral("type")] = QStringLiteral("Озвучка");
        row[QStringLiteral("episodes")] = 0;
        list << row;
    }

    static const QRegularExpression epRe(QStringLiteral("data-episode-number=\"(\\d+)\""));
    auto epIt = epRe.globalMatch(html);
    while (epIt.hasNext()) {
        maxEp = qMax(maxEp, epIt.next().captured(1).toInt());
    }
    if (totalEpisodes)
        *totalEpisodes = maxEp;

    for (QVariant &rv : list) {
        QVariantMap row = rv.toMap();
        if (maxEp > 0)
            row[QStringLiteral("episodes")] = maxEp;
        rv = row;
    }
    return list;
}

QString playerUrlForEpisode(const QString &animegoId, int episode) {
    if (episode <= 1)
        return kAnimegoBase + QStringLiteral("/player/") + animegoId;
    return QString();
}

QJsonArray parseCvhPlaylistItems(const QString &body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
    if (doc.isArray())
        return doc.array();
    if (doc.isObject())
        return doc.object().value(QStringLiteral("items")).toArray();
    return {};
}

QString pickStreamUrl(const QJsonObject &sources) {
    // okcdn HLS — master+variant с подписанными относительными путями; mpv через SOCKS5
    // часто падает с «loading failed». Прогрессивный MP4 тот же CDN, но стабильнее.
    static const QStringList mpegKeys = {
        QStringLiteral("mpegFullHdUrl"),
        QStringLiteral("mpegHighUrl"),
        QStringLiteral("mpegQhdUrl"),
        QStringLiteral("mpegMediumUrl"),
        QStringLiteral("mpegLowUrl"),
        QStringLiteral("mpegLowestUrl"),
        QStringLiteral("mpegTinyUrl"),
        QStringLiteral("mpeg4kUrl"),
        QStringLiteral("mpeg2kUrl"),
    };
    for (const QString &key : mpegKeys) {
        const QString url = sources.value(key).toString();
        if (url.startsWith(QStringLiteral("http")))
            return url;
    }
    const QString hls = sources.value(QStringLiteral("hlsUrl")).toString();
    if (!hls.isEmpty())
        return hls;
    QString dash = sources.value(QStringLiteral("dashUrl")).toString();
    if (dash.isEmpty())
        dash = sources.value(QStringLiteral("dashManifestUrl")).toString();
    if (!dash.isEmpty())
        return dash;
    int bestQ = 0;
    QString best;
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        if (!it.key().startsWith(QLatin1String("url")))
            continue;
        const QString url = it.value().toString();
        if (!url.startsWith(QStringLiteral("http")))
            continue;
        const int q = it.key().mid(3).toInt();
        if (q >= bestQ) {
            bestQ = q;
            best = url;
        }
    }
    return best;
}

class AnimegoSession : public QObject {
public:
    explicit AnimegoSession(QObject *parent = nullptr) : QObject(parent) {}

    void loadTranslations(
        const QString &title, const QString &originalTitle, const QString &englishTitle,
        const QString &kind, int year, AnimegoClient::TranslationsCallback callback) {
        Q_UNUSED(kind);
        const QStringList queries = buildSearchQueries(title, originalTitle, englishTitle);
        QStringList wantedTitles;
        const auto addWanted = [&](const QString &s) {
            const QString t = s.trimmed();
            if (!t.isEmpty() && !wantedTitles.contains(t, Qt::CaseInsensitive))
                wantedTitles.append(t);
        };
        addWanted(englishTitle);
        addWanted(originalTitle);
        addWanted(title);

        if (queries.isEmpty()) {
            callback({}, QStringLiteral("Нет названия для поиска на AnimeGO"));
            return;
        }

        auto accumulated = std::make_shared<QVariantList>();
        auto searchErr = std::make_shared<QString>();
        runSearchQueries(queries, 0, accumulated, searchErr, wantedTitles, year, callback);
    }

    void getEpisodeStream(
        const QString &animegoId, int episode, const QString &translationId,
        AnimegoClient::StreamCallback callback) {
        if (animegoId != m_animegoId) {
            callback({}, QStringLiteral("Сначала загрузи озвучки CVH"));
            return;
        }
        const QString bareId = translationId.startsWith(kTranslationPrefix)
            ? translationId.mid(kTranslationPrefix.size())
            : translationId;
        const CvhVoice *voice = nullptr;
        for (const CvhVoice &v : m_voices) {
            if (v.translationId == bareId) {
                voice = &v;
                break;
            }
        }
        if (!voice) {
            callback({}, QStringLiteral("Озвучка CVH не найдена"));
            return;
        }

        fetchVoicesHtml(animegoId, episode, [this, episode, voice, callback](QString html, QString err) {
            if (html.isEmpty()) {
                callback({}, err);
                return;
            }
            QList<CvhVoice> fresh;
            int total = 0;
            parseCvhVoices(html, &fresh, &total);
            QString cvhId = voice->cvhId;
            QString label = voice->label;
            for (const CvhVoice &v : fresh) {
                if (v.translationId == voice->translationId) {
                    cvhId = v.cvhId;
                    label = v.label;
                    break;
                }
            }
            fetchCvhStream(cvhId, episode, label, callback);
        });
    }

private:
    QString m_animegoId;
    QList<CvhVoice> m_voices;

    void searchAnime(const QString &query, std::function<void(QVariantList, QString)> callback) {
        QUrl url(kAnimegoBase + QStringLiteral("/search/anime"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), query.trimmed());
        url.setQuery(q);
        httpGet(url,
                [callback, query](QString body, QString err) {
                    if (body.isEmpty()) {
                        callback({}, err);
                        return;
                    }
                    const QVariantList results = parseSearchResults(body);
                    qInfo("Animego search %s -> %d results (%d bytes)", qUtf8Printable(query.trimmed()),
                          results.size(), body.size());
                    if (results.isEmpty()) {
                        callback({}, QStringLiteral("По запросу ничего не найдено на AnimeGO"));
                        return;
                    }
                    callback(results, {});
                },
                AnimegoRequestKind::SearchHtml);
    }

    void runSearchQueries(
        const QStringList &queries, int index, std::shared_ptr<QVariantList> accumulated,
        std::shared_ptr<QString> searchErr, const QStringList &wantedTitles, int year,
        AnimegoClient::TranslationsCallback callback) {
        if (index >= queries.size()) {
            const QVariantList ranked = rankSearchResults(*accumulated, wantedTitles, year);
            if (ranked.isEmpty()) {
                callback({}, searchErr->isEmpty() ? QStringLiteral("Тайтл не найден на AnimeGO")
                                                  : *searchErr);
                return;
            }
            tryCandidates(ranked, 0, callback);
            return;
        }

        searchAnime(queries.at(index), [this, queries, index, accumulated, searchErr, wantedTitles, year,
                                        callback](QVariantList results, QString err) {
            if (!err.isEmpty() && searchErr->isEmpty())
                *searchErr = err;
            QSet<QString> seen;
            for (const QVariant &rv : *accumulated)
                seen.insert(rv.toMap().value(QStringLiteral("id")).toString());
            for (const QVariant &rv : results) {
                const QString id = rv.toMap().value(QStringLiteral("id")).toString();
                if (!id.isEmpty() && !seen.contains(id)) {
                    seen.insert(id);
                    accumulated->append(rv);
                }
            }
            QTimer::singleShot(jitterMs(250, 700), [this, queries, index, accumulated, searchErr, wantedTitles,
                                                     year, callback]() {
                runSearchQueries(queries, index + 1, accumulated, searchErr, wantedTitles, year, callback);
            });
        });
    }

    void tryCandidates(const QVariantList &ranked, int index, AnimegoClient::TranslationsCallback callback) {
        if (index >= ranked.size()) {
            callback({}, QStringLiteral("На AnimeGO нет озвучек CVH для этого тайтла"));
            return;
        }
        const QVariantMap match = ranked.at(index).toMap();
        const QString animegoId = match.value(QStringLiteral("id")).toString();
        const QString matchTitle = match.value(QStringLiteral("title")).toString();
        if (animegoId.isEmpty()) {
            tryCandidates(ranked, index + 1, std::move(callback));
            return;
        }

        fetchVoicesHtml(animegoId, 1, [this, ranked, index, animegoId, matchTitle,
                                         callback](QString html, QString pageErr) {
            if (html.isEmpty()) {
                qWarning("Animego CVH: player %s (%s) — %s", qUtf8Printable(animegoId),
                         qUtf8Printable(matchTitle), qUtf8Printable(pageErr));
                QTimer::singleShot(jitterMs(250, 700), [this, ranked, index, callback]() {
                    tryCandidates(ranked, index + 1, callback);
                });
                return;
            }
            QList<CvhVoice> voices;
            int totalEpisodes = 0;
            const QVariantList list = parseCvhVoices(html, &voices, &totalEpisodes);
            if (list.isEmpty()) {
                qInfo("Animego CVH: %s (%s) — нет CVH, пробуем следующий кандидат",
                      qUtf8Printable(animegoId), qUtf8Printable(matchTitle));
                QTimer::singleShot(jitterMs(250, 700), [this, ranked, index, callback]() {
                    tryCandidates(ranked, index + 1, callback);
                });
                return;
            }
            m_animegoId = animegoId;
            m_voices = voices;
            QVariantList withMeta = list;
            for (QVariant &rv : withMeta) {
                QVariantMap row = rv.toMap();
                row[QStringLiteral("animegoId")] = animegoId;
                rv = row;
            }
            qInfo("Animego CVH: %s (%s) — %d voices, %d episodes", qUtf8Printable(animegoId),
                  qUtf8Printable(matchTitle), withMeta.size(), totalEpisodes);
            callback(withMeta, {});
        });
    }

    void fetchVoicesHtml(const QString &animegoId, int episode, StringCallback callback) {
        const QString firstUrl = kAnimegoBase + QStringLiteral("/player/") + animegoId;
        if (episode <= 1) {
            httpGet(QUrl(firstUrl),
                    [callback](QString body, QString err) {
                        if (body.isEmpty()) {
                            callback({}, err);
                            return;
                        }
                        callback(extractPlayerHtml(body), {});
                    },
                    AnimegoRequestKind::PlayerHtml);
            return;
        }
        httpGet(QUrl(firstUrl),
                [animegoId, episode, callback](QString body, QString err) {
            if (body.isEmpty()) {
                callback({}, err);
                return;
            }
            const QString html = extractPlayerHtml(body);
            static const QRegularExpression epBlockRe(
                QStringLiteral("data-episode-number=\"(\\d+)\"[^>]*data-episode=\"([^\"]+)\""));
            QString videoToken;
            auto it = epBlockRe.globalMatch(html);
            while (it.hasNext()) {
                auto m = it.next();
                if (m.captured(1).toInt() == episode) {
                    videoToken = m.captured(2);
                    break;
                }
            }
            if (videoToken.isEmpty()) {
                callback({}, QStringLiteral("Серия %1 недоступна на AnimeGO").arg(episode));
                return;
            }
            const QString url = kAnimegoBase + QStringLiteral("/player/videos/") + videoToken;
            httpGet(QUrl(url),
                    [callback](QString body, QString err2) {
                        if (body.isEmpty()) {
                            callback({}, err2);
                            return;
                        }
                        callback(extractPlayerHtml(body), {});
                    },
                    AnimegoRequestKind::PlayerHtml);
                },
                AnimegoRequestKind::PlayerHtml);
    }

    void fetchCvhStream(const QString &cvhId, int episode, const QString &label, AnimegoClient::StreamCallback callback) {
        const QUrl playlistUrl(
            kCvhApiBase + QStringLiteral("/playlist?pub=") + kCvhPub + QStringLiteral("&aggr=") + kCvhAggr
            + QStringLiteral("&id=") + cvhId);
        httpGet(playlistUrl,
                [cvhId, episode, label, callback](QString body, QString err) {
                    if (body.isEmpty()) {
                        callback({}, err.isEmpty() ? QStringLiteral("CVH playlist пуст") : err);
                        return;
                    }
                    const QJsonArray items = parseCvhPlaylistItems(body);
                    if (items.isEmpty()) {
                        qWarning("Animego CVH: playlist id=%s — empty items (%d bytes, preview: %.120s)",
                                 qUtf8Printable(cvhId), body.size(), qUtf8Printable(body.left(120)));
                        callback({}, QStringLiteral("CVH не вернул эпизоды"));
                        return;
                    }

                    QMap<int, QMap<int, QJsonArray>> bySeason;
                    for (const QJsonValue &iv : items) {
                        const QJsonObject item = iv.toObject();
                        const int season = item.value(QStringLiteral("season")).toInt(1);
                        const int ep = item.value(QStringLiteral("episode")).toInt();
                        bySeason[season][ep].append(item);
                    }

                    int season = 1;
                    if (bySeason.size() == 1)
                        season = bySeason.firstKey();
                    if (!bySeason.contains(season) || !bySeason[season].contains(episode)) {
                        callback({}, QStringLiteral("Серия %1 не найдена в CVH").arg(episode));
                        return;
                    }

                    QStringList studios;
                    for (const QJsonValue &iv : bySeason[season][episode]) {
                        const QString studio = iv.toObject().value(QStringLiteral("voiceStudio")).toString();
                        if (!studio.isEmpty() && !studios.contains(studio))
                            studios.append(studio);
                    }
                    const QString matched = matchCvhStudio(label, studios);
                    if (matched.isEmpty()) {
                        callback({},
                                 QStringLiteral("Озвучка «%1» не найдена в CVH (доступно: %2)")
                                     .arg(label, studios.join(QStringLiteral(", "))));
                        return;
                    }

                    QString vkId;
                    for (const QJsonValue &iv : bySeason[season][episode]) {
                        const QJsonObject item = iv.toObject();
                        if (item.value(QStringLiteral("voiceStudio")).toString() == matched) {
                            vkId = item.value(QStringLiteral("vkId")).toString();
                            break;
                        }
                    }
                    if (vkId.isEmpty()) {
                        callback({}, QStringLiteral("CVH vkId не найден"));
                        return;
                    }

                    const QUrl videoUrl(kCvhApiBase + QStringLiteral("/video/") + vkId);
                    httpGet(videoUrl,
                            [callback](QString vbody, QString verr) {
                                if (vbody.isEmpty()) {
                                    callback({}, verr);
                                    return;
                                }
                                const QJsonObject sources =
                                    QJsonDocument::fromJson(vbody.toUtf8()).object().value(QStringLiteral("sources")).toObject();
                                const QString url = pickStreamUrl(sources);
                                if (url.isEmpty()) {
                                    callback({}, QStringLiteral("CVH не вернул ссылку на поток"));
                                    return;
                                }
                                const QString kind = url.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive)
                                    ? QStringLiteral("hls")
                                    : url.contains(QStringLiteral(".mpd"), Qt::CaseInsensitive)
                                        ? QStringLiteral("dash")
                                        : QStringLiteral("mp4");
                                qInfo("Animego CVH: stream %s (%s)", qUtf8Printable(kind),
                                      qUtf8Printable(QUrl(url).host()));
                                callback(url, {});
                            },
                            AnimegoRequestKind::JsonApi, 3, true);
                },
                AnimegoRequestKind::JsonApi, 3, true);
    }
};

QSharedPointer<AnimegoSession> sharedSession() {
    static QSharedPointer<AnimegoSession> session(new AnimegoSession());
    return session;
}

} // namespace

void AnimegoClient::loadTranslations(
    const QString &title, const QString &originalTitle, const QString &englishTitle,
    const QString &kind, int year, TranslationsCallback callback) {
    sharedSession()->loadTranslations(title, originalTitle, englishTitle, kind, year, std::move(callback));
}

void AnimegoClient::getEpisodeStream(
    const QString &animegoId, int episode, const QString &translationId, StreamCallback callback) {
    sharedSession()->getEpisodeStream(animegoId, episode, translationId, std::move(callback));
}