#include "AnimetkaClient.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace {

const QString kBase = QStringLiteral("https://animetka.com");
const QString kUa = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");

using StringCallback = std::function<void(QString body, QString error)>;

void httpGetJson(const QUrl &url, StringCallback callback) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Referer", "https://animetka.com/");
    req.setRawHeader("Origin", "https://animetka.com");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(30000);

    QNetworkReply *reply = NetworkManager::instance()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            callback({}, QStringLiteral("Animetka HTTP %1: %2").arg(status).arg(reply->errorString()));
            return;
        }
        callback(QString::fromUtf8(reply->readAll()), {});
    });
}

QString normalizeTitle(QString s) {
    s = s.trimmed().toLower();
    static const QRegularExpression re(QStringLiteral("[^\\p{L}\\p{N}]+"));
    s.replace(re, QStringLiteral(" "));
    return s.simplified();
}

int titleScore(const QString &candidate, const QStringList &needles, int candYear, int wantYear) {
    const QString c = normalizeTitle(candidate);
    if (c.isEmpty())
        return -1;
    int best = 0;
    for (const QString &n : needles) {
        const QString nn = normalizeTitle(n);
        if (nn.isEmpty())
            continue;
        if (c == nn)
            best = qMax(best, 100);
        else if (c.contains(nn) || nn.contains(c))
            best = qMax(best, 70);
        else {
            // token overlap
            const QStringList a = c.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const QStringList b = nn.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            int hit = 0;
            for (const QString &t : b) {
                if (t.size() >= 3 && a.contains(t))
                    ++hit;
            }
            if (hit > 0)
                best = qMax(best, 30 + hit * 10);
        }
    }
    if (best <= 0)
        return -1;
    if (wantYear > 0 && candYear > 0) {
        if (candYear == wantYear)
            best += 25;
        else if (qAbs(candYear - wantYear) <= 1)
            best += 10;
        else
            best -= 15;
    }
    return best;
}

// PlayerJS file: "[360]url,[480]url,[720]url" — split only before next [quality]
QMap<int, QString> parseQualityFileField(const QString &file) {
    QMap<int, QString> out;
    if (file.isEmpty())
        return out;
    // Надёжнее, чем «жадный» regex: делим по ",(?=[digits])"
    const QStringList parts = file.split(QRegularExpression(QStringLiteral(",(?=\\[\\d+\\])")),
                                         Qt::SkipEmptyParts);
    static const QRegularExpression one(QStringLiteral(R"(^\[(\d+)\]\s*(.+)$)"));
    for (QString p : parts) {
        p = p.trimmed();
        while (p.endsWith(QLatin1Char(',')))
            p.chop(1);
        const auto m = one.match(p.trimmed());
        if (!m.hasMatch())
            continue;
        const int q = m.captured(1).toInt();
        const QString url = m.captured(2).trimmed();
        if (q > 0 && !url.isEmpty())
            out.insert(q, url);
    }
    // запасной разбор: все https-ссылки рядом с [quality]
    if (out.isEmpty()) {
        static const QRegularExpression httpsRe(
            QStringLiteral(R"(\[(\d+)\](https?://[^\s,\[]+))"));
        auto it = httpsRe.globalMatch(file);
        while (it.hasNext()) {
            const auto m = it.next();
            out.insert(m.captured(1).toInt(), m.captured(2).trimmed());
        }
    }
    return out;
}

// Сырой кусок из playlist.file → абсолютный URL, который можно отдать mpv.
// Важно: "//video/1/720.m3u8?code=//kodikplayer.com/..." — это НЕ protocol-relative
// на чужой хост, а прокси Animetka: https://animetka.com/video/1/720.m3u8?code=...
QString absolutizeStreamUrl(QString raw) {
    raw = raw.trimmed();
    if (raw.isEmpty())
        return {};

    if (raw.startsWith(QLatin1String("http://")) || raw.startsWith(QLatin1String("https://")))
        return raw;

    // Animetka video-proxy (PlayerJS file field)
    QString path = raw;
    if (path.startsWith(QLatin1String("//video/")))
        path = path.mid(1); // /video/...
    if (path.startsWith(QLatin1String("/video/")))
        return kBase + path;

    // обычный protocol-relative CDN
    if (raw.startsWith(QLatin1String("//")))
        return QStringLiteral("https:") + raw;

    if (raw.startsWith(QLatin1Char('/')))
        return kBase + raw;

    return {};
}

QString absolutizeUrl(QString url) {
    return absolutizeStreamUrl(url);
}

// URL, который mpv может открыть напрямую (без отдельного Kodik get_links).
bool isDirectPlayable(const QString &url) {
    if (url.isEmpty())
        return false;
    if (!(url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"))))
        return false;
    // Голый embed kodikplayer.com/serial/... — mpv не играет; нужен proxy или get_links.
    // А вот animetka.com/video/...?code=//kodikplayer.com/... — уже готовый m3u8.
    if (url.contains(QStringLiteral("kodikplayer.com"), Qt::CaseInsensitive)
        && !url.contains(QStringLiteral("/video/"), Qt::CaseInsensitive))
        return false;
    return true;
}

// code=amdvk:ID → plapi sources → pick quality
void resolveAmdVk(const QString &vkId, int wantQ, std::function<void(QString url)> done) {
    if (vkId.isEmpty()) {
        done({});
        return;
    }
    const QUrl url(QStringLiteral("https://plapi.cdnvideohub.com/api/v1/player/sv/video/%1").arg(vkId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    req.setRawHeader("Referer", "https://amd.online/");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = NetworkManager::instance()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, wantQ, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({});
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject sources = root.value(QStringLiteral("sources")).toObject();
        struct Tier { int q; QStringList fields; };
        const QList<Tier> tiers = {
            {2160, {QStringLiteral("mpeg4kUrl")}},
            {1440, {QStringLiteral("mpeg2kUrl"), QStringLiteral("mpegQhdUrl")}},
            {1080, {QStringLiteral("mpegFullHdUrl")}},
            {720, {QStringLiteral("mpegHighUrl")}},
            {480, {QStringLiteral("mpegMediumUrl"), QStringLiteral("mpegUrl")}},
            {360, {QStringLiteral("mpegLowUrl")}},
        };
        auto pickField = [&](const Tier &t) -> QString {
            for (const QString &f : t.fields) {
                const QString v = sources.value(f).toString().trimmed();
                if (!v.isEmpty())
                    return absolutizeUrl(v.startsWith(QLatin1String("http")) ? v : (QStringLiteral("https:") + v));
            }
            return {};
        };
        QString best;
        int bestQ = -1;
        for (const Tier &t : tiers) {
            if (wantQ > 0 && t.q > wantQ)
                continue;
            const QString u = pickField(t);
            if (!u.isEmpty() && t.q > bestQ) {
                bestQ = t.q;
                best = u;
            }
        }
        if (best.isEmpty()) {
            for (const Tier &t : tiers) {
                const QString u = pickField(t);
                if (!u.isEmpty() && t.q > bestQ) {
                    bestQ = t.q;
                    best = u;
                }
            }
        }
        if (best.isEmpty()) {
            const QString hls = sources.value(QStringLiteral("hlsUrl")).toString().trimmed();
            if (!hls.isEmpty())
                best = absolutizeUrl(hls.startsWith(QLatin1String("http")) ? hls : (QStringLiteral("https:") + hls));
        }
        done(best);
    });
}

// 1) прямые HLS (libria/cdn) — сразу
// 2) amdvk через plapi
// 3) только в конце — kodik_embed (хрупкий прокси)
void pickUrlFromQualities(const QMap<int, QString> &map, const QString &qualityHint,
                          std::function<void(QString url, QString err)> done) {
    if (map.isEmpty()) {
        done({}, QStringLiteral("Animetka: нет потоков для серии"));
        return;
    }

    const int want = qualityHint.trimmed().toInt();
    QList<int> keys = map.keys();
    std::sort(keys.begin(), keys.end(), std::greater<int>());

    // --- фаза 1: синхронно собрать все прямые URL ---
    QList<QPair<int, QString>> direct; // quality → url
    QList<QPair<int, QString>> amdJobs;
    QString kodikEmbed;

    auto considerRaw = [&](int q, const QString &raw) {
        static const QRegularExpression amdRe(QStringLiteral(R"(code=amdvk:([^&,\s]+))"));
        const auto amd = amdRe.match(raw);
        if (amd.hasMatch()) {
            amdJobs.append({q, amd.captured(1)});
            return;
        }

        // Явный https в сырой строке (даже если вокруг мусор)
        static const QRegularExpression httpsRe(
            QStringLiteral(R"((https?://[^\s,\"'<>]+))"));
        const auto hm = httpsRe.match(raw);
        if (hm.hasMatch()) {
            const QString u = hm.captured(1).trimmed();
            if (isDirectPlayable(u)) {
                direct.append({q, u});
                return;
            }
            if (u.contains(QStringLiteral("kodikplayer.com")) && kodikEmbed.isEmpty())
                kodikEmbed = u;
        }

        const QString abs = absolutizeStreamUrl(raw);
        if (isDirectPlayable(abs)) {
            direct.append({q, abs});
            return;
        }
        // code=//kodikplayer... → можно собрать proxy URL сами
        static const QRegularExpression codeRe(
            QStringLiteral(R"(code=((?://|https?://)[^&,\s]+))"));
        const auto cm = codeRe.match(raw);
        if (cm.hasMatch()) {
            const QString codeVal = cm.captured(1);
            // предпочтительно animetka proxy с нужным качеством
            const QString proxy = QStringLiteral("%1/video/1/%2.m3u8?code=%3")
                                      .arg(kBase)
                                      .arg(q > 0 ? q : 720)
                                      .arg(codeVal);
            if (isDirectPlayable(proxy))
                direct.append({q, proxy});
            const QString c = absolutizeStreamUrl(codeVal);
            if (c.contains(QStringLiteral("kodikplayer.com")) && kodikEmbed.isEmpty())
                kodikEmbed = c;
        }
    };

    for (int q : keys)
        considerRaw(q, map.value(q));

    auto pickDirect = [&]() -> QString {
        if (direct.isEmpty())
            return {};
        // точное качество
        if (want > 0) {
            for (const auto &p : direct) {
                if (p.first == want)
                    return p.second;
            }
            // ближайшее ≤ want
            QString best;
            int bestQ = -1;
            for (const auto &p : direct) {
                if (p.first <= want && p.first > bestQ) {
                    bestQ = p.first;
                    best = p.second;
                }
            }
            if (!best.isEmpty())
                return best;
        }
        // максимальное
        QString best = direct.first().second;
        int bestQ = direct.first().first;
        for (const auto &p : direct) {
            if (p.first > bestQ) {
                bestQ = p.first;
                best = p.second;
            }
        }
        return best;
    };

    const QString directUrl = pickDirect();
    if (!directUrl.isEmpty()) {
        qInfo("Animetka: direct HLS q-hint=%s host=%s",
              qUtf8Printable(qualityHint), qUtf8Printable(QUrl(directUrl).host()));
        done(directUrl, {});
        return;
    }

    // --- фаза 2: amdvk асинхронно ---
    if (!amdJobs.isEmpty()) {
        // сортируем: want, потом выше
        std::sort(amdJobs.begin(), amdJobs.end(), [want](const QPair<int, QString> &a, const QPair<int, QString> &b) {
            if (want > 0) {
                const int da = qAbs(a.first - want);
                const int db = qAbs(b.first - want);
                if (da != db)
                    return da < db;
            }
            return a.first > b.first;
        });

        struct AmdState {
            QList<QPair<int, QString>> jobs;
            std::function<void(QString, QString)> done;
            QString kodikEmbed;
            int idx = 0;
        };
        auto st = std::make_shared<AmdState>();
        st->jobs = amdJobs;
        st->done = std::move(done);
        st->kodikEmbed = kodikEmbed;

        auto tryAmd = std::make_shared<std::function<void()>>();
        *tryAmd = [st, tryAmd]() {
            if (st->idx >= st->jobs.size()) {
                if (!st->kodikEmbed.isEmpty())
                    st->done(st->kodikEmbed, QStringLiteral("kodik_embed"));
                else
                    st->done({}, QStringLiteral("Animetka: нет прямого потока"));
                return;
            }
            const auto job = st->jobs.at(st->idx++);
            resolveAmdVk(job.second, job.first, [st, tryAmd](QString url) {
                if (isDirectPlayable(url)) {
                    qInfo("Animetka: amdvk resolved host=%s", qUtf8Printable(QUrl(url).host()));
                    st->done(url, {});
                    return;
                }
                (*tryAmd)();
            });
        };
        (*tryAmd)();
        return;
    }

    // --- фаза 3: только kodik ---
    if (!kodikEmbed.isEmpty()) {
        done(kodikEmbed, QStringLiteral("kodik_embed"));
        return;
    }
    done({}, QStringLiteral("Animetka: не удалось получить ссылку"));
}

} // namespace

bool AnimetkaClient::parseTranslationId(const QString &id, QString *tidOut, QString *qualityOut) {
    if (!id.startsWith(QLatin1String("animetka_")))
        return false;
    QString rest = id.mid(9); // after "animetka_"
    QString quality;
    const int qPos = rest.indexOf(QStringLiteral("_q"));
    if (qPos > 0) {
        quality = rest.mid(qPos + 2);
        rest = rest.left(qPos);
    }
    if (rest.isEmpty())
        return false;
    if (tidOut)
        *tidOut = rest;
    if (qualityOut)
        *qualityOut = quality;
    return true;
}

QString AnimetkaClient::makeTranslationId(const QString &tid, const QString &quality) {
    if (quality.isEmpty() || quality == QLatin1String("best"))
        return QStringLiteral("animetka_") + tid;
    return QStringLiteral("animetka_") + tid + QStringLiteral("_q") + quality;
}

void AnimetkaClient::loadTranslations(const QString &shikimoriId,
                                      const QString &title,
                                      const QString &originalTitle,
                                      const QString &englishTitle,
                                      int year,
                                      TranslationsCallback callback) {
    QStringList queries;
    auto addQ = [&](const QString &q) {
        const QString t = q.trimmed();
        if (t.size() < 2)
            return;
        if (!queries.contains(t, Qt::CaseInsensitive))
            queries << t;
    };
    addQ(title);
    addQ(originalTitle);
    addQ(englishTitle);
    // короткие варианты
    if (title.contains(QLatin1Char('[')))
        addQ(title.left(title.indexOf(QLatin1Char('['))).trimmed());

    if (queries.isEmpty()) {
        callback({}, QStringLiteral("Animetka: нет названия для поиска"));
        return;
    }

    QPointer<AnimetkaClient> self(this);
    // search first query, score candidates, fetch detail for shikimori match
    auto searchAt = std::make_shared<std::function<void(int)>>();
    *searchAt = [self, shikimoriId, queries, year, callback, searchAt](int qIdx) {
        if (!self) {
            callback({}, QStringLiteral("cancelled"));
            return;
        }
        if (qIdx >= queries.size()) {
            callback({}, QStringLiteral("Animetka: тайтл не найден"));
            return;
        }
        const QString searchName = queries.at(qIdx);
        QUrl searchUrl(kBase + QStringLiteral("/api/anime/search"));
        QUrlQuery qq;
        qq.addQueryItem(QStringLiteral("name"), searchName);
        qq.addQueryItem(QStringLiteral("limit"), QStringLiteral("12"));
        qq.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
        searchUrl.setQuery(qq);

        httpGetJson(searchUrl, [self, shikimoriId, queries, year, callback, searchName, searchAt, qIdx](QString body, QString err) {
        if (!self) {
            callback({}, QStringLiteral("cancelled"));
            return;
        }
        if (!err.isEmpty()) {
            (*searchAt)(qIdx + 1);
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(body.toUtf8()).array();
        struct Cand { int id; int score; QString title; int year; };
        QList<Cand> cands;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const int id = o.value(QStringLiteral("animetka_id")).toInt();
            if (id <= 0)
                continue;
            const QString t = o.value(QStringLiteral("title")).toString();
            const QString orig = o.value(QStringLiteral("title_orig")).toString();
            const int y = o.value(QStringLiteral("year")).toInt();
            const int sc = qMax(titleScore(t, queries, y, year), titleScore(orig, queries, y, year));
            if (sc < 40)
                continue;
            cands.push_back({id, sc, t, y});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.score > b.score; });
        if (cands.isEmpty()) {
            (*searchAt)(qIdx + 1);
            return;
        }

        // Проверяем top-N деталями на shikimori_id
        const int checkN = qMin(5, cands.size());
        auto state = std::make_shared<int>(0);
        auto found = std::make_shared<int>(0);

        auto tryDetail = std::make_shared<std::function<void(int)>>();
        *tryDetail = [self, cands, checkN, shikimoriId, state, found, callback, tryDetail](int idx) {
            if (!self)
                return;
            if (*found > 0)
                return;
            if (idx >= checkN) {
                // fallback: лучший score без shiki match
                if (!cands.isEmpty()) {
                    const int mid = cands.first().id;
                    self->m_materialId = QString::number(mid);
                    // playlist with any tid — need Animes from detail first
                    QUrl du(kBase + QStringLiteral("/api/anime/") + QString::number(mid));
                    httpGetJson(du, [self, mid, callback](QString body, QString err) {
                        if (!self)
                            return;
                        if (!err.isEmpty()) {
                            callback({}, err);
                            return;
                        }
                        const QJsonObject det = QJsonDocument::fromJson(body.toUtf8()).object();
                        const QJsonArray animes = det.value(QStringLiteral("Animes")).toArray();
                        int tid = 0;
                        if (!animes.isEmpty())
                            tid = animes.at(0).toObject().value(QStringLiteral("translation")).toInt();
                        if (tid <= 0) {
                            callback({}, QStringLiteral("Animetka: нет озвучек в карточке"));
                            return;
                        }
                        QUrl pu(kBase + QStringLiteral("/api/anime/playlist"));
                        QUrlQuery pq;
                        pq.addQueryItem(QStringLiteral("material"), QString::number(mid));
                        pq.addQueryItem(QStringLiteral("tid"), QString::number(tid));
                        pu.setQuery(pq);
                        httpGetJson(pu, [self, mid, callback](QString pbody, QString perr) {
                            if (!self)
                                return;
                            if (!perr.isEmpty()) {
                                callback({}, perr);
                                return;
                            }
                            const QJsonObject pl = QJsonDocument::fromJson(pbody.toUtf8()).object();
                            const QJsonArray trs = pl.value(QStringLiteral("translations")).toArray();
                            QVariantList out;
                            for (const QJsonValue &tv : trs) {
                                const QJsonObject t = tv.toObject();
                                const int tidNum = t.value(QStringLiteral("value")).toInt();
                                if (tidNum <= 0)
                                    continue;
                                const QString name = t.value(QStringLiteral("name")).toString().trimmed();
                                if (name.isEmpty())
                                    continue;
                                const int ep = t.value(QStringLiteral("episode_count")).toInt();
                                const bool subs = t.value(QStringLiteral("subs")).toBool();
                                QStringList qlist;
                                for (const QJsonValue &qv : t.value(QStringLiteral("quality")).toArray())
                                    qlist << qv.toString();
                                if (qlist.isEmpty()) {
                                    for (const QJsonValue &qv : pl.value(QStringLiteral("qualities")).toArray())
                                        qlist << qv.toString();
                                }
                                QVariantMap row;
                                const QString tidStr = QString::number(tidNum);
                                row[QStringLiteral("id")] = makeTranslationId(tidStr);
                                row[QStringLiteral("tid")] = tidStr;
                                row[QStringLiteral("name")] = name + (ep > 0 ? QStringLiteral(" (%1 эп.)").arg(ep) : QString());
                                row[QStringLiteral("title")] = row[QStringLiteral("name")];
                                row[QStringLiteral("type")] = subs ? QStringLiteral("Субтитры") : QStringLiteral("Озвучка");
                                row[QStringLiteral("episodes")] = ep;
                                row[QStringLiteral("qualities")] = qlist;
                                row[QStringLiteral("materialId")] = QString::number(mid);
                                self->m_tidToMaterial[tidStr] = mid;
                                out << row;
                            }
                            if (out.isEmpty())
                                callback({}, QStringLiteral("Animetka: пустой список озвучек"));
                            else
                                callback(out, {});
                        });
                    });
                    return;
                }
                callback({}, QStringLiteral("Animetka: нет совпадения"));
                return;
            }

            const int mid = cands.at(idx).id;
            QUrl du(kBase + QStringLiteral("/api/anime/") + QString::number(mid));
            httpGetJson(du, [self, mid, shikimoriId, idx, state, found, callback, tryDetail](QString body, QString err) {
                if (!self || *found > 0)
                    return;
                ++(*state);
                if (err.isEmpty()) {
                    const QJsonObject det = QJsonDocument::fromJson(body.toUtf8()).object();
                    const QString sk = QString::number(det.value(QStringLiteral("shikimori_id")).toInt());
                    const bool match = !shikimoriId.isEmpty() && sk == shikimoriId;
                    // если shikimori пустой на карточке — принимаем высокий score
                    if (match || (shikimoriId.isEmpty() && idx == 0) || (sk == QLatin1String("0") && idx == 0)) {
                        *found = mid;
                        self->m_materialId = QString::number(mid);
                        const QJsonArray animes = det.value(QStringLiteral("Animes")).toArray();
                        int tid = 0;
                        if (!animes.isEmpty())
                            tid = animes.at(0).toObject().value(QStringLiteral("translation")).toInt();
                        if (tid <= 0) {
                            callback({}, QStringLiteral("Animetka: нет озвучек"));
                            return;
                        }
                        QUrl pu(kBase + QStringLiteral("/api/anime/playlist"));
                        QUrlQuery pq;
                        pq.addQueryItem(QStringLiteral("material"), QString::number(mid));
                        pq.addQueryItem(QStringLiteral("tid"), QString::number(tid));
                        pu.setQuery(pq);
                        httpGetJson(pu, [self, mid, callback](QString pbody, QString perr) {
                            if (!self)
                                return;
                            if (!perr.isEmpty()) {
                                callback({}, perr);
                                return;
                            }
                            const QJsonObject pl = QJsonDocument::fromJson(pbody.toUtf8()).object();
                            const QJsonArray trs = pl.value(QStringLiteral("translations")).toArray();
                            QVariantList out;
                            for (const QJsonValue &tv : trs) {
                                const QJsonObject t = tv.toObject();
                                const int tidNum = t.value(QStringLiteral("value")).toInt();
                                if (tidNum <= 0)
                                    continue;
                                const QString name = t.value(QStringLiteral("name")).toString().trimmed();
                                if (name.isEmpty())
                                    continue;
                                const int ep = t.value(QStringLiteral("episode_count")).toInt();
                                const bool subs = t.value(QStringLiteral("subs")).toBool();
                                QStringList qlist;
                                for (const QJsonValue &qv : t.value(QStringLiteral("quality")).toArray())
                                    qlist << qv.toString();
                                if (qlist.isEmpty()) {
                                    for (const QJsonValue &qv : pl.value(QStringLiteral("qualities")).toArray())
                                        qlist << qv.toString();
                                }
                                QVariantMap row;
                                const QString tidStr = QString::number(tidNum);
                                row[QStringLiteral("id")] = makeTranslationId(tidStr);
                                row[QStringLiteral("tid")] = tidStr;
                                row[QStringLiteral("name")] = name + (ep > 0 ? QStringLiteral(" (%1 эп.)").arg(ep) : QString());
                                row[QStringLiteral("title")] = row[QStringLiteral("name")];
                                row[QStringLiteral("type")] = subs ? QStringLiteral("Субтитры") : QStringLiteral("Озвучка");
                                row[QStringLiteral("episodes")] = ep;
                                row[QStringLiteral("qualities")] = qlist;
                                row[QStringLiteral("materialId")] = QString::number(mid);
                                self->m_tidToMaterial[tidStr] = mid;
                                out << row;
                            }
                            if (out.isEmpty())
                                callback({}, QStringLiteral("Animetka: пустой список озвучек"));
                            else {
                                qInfo("Animetka: material=%d translations=%d", mid, out.size());
                                callback(out, {});
                            }
                        });
                        return;
                    }
                }
                (*tryDetail)(idx + 1);
            });
        };
        (*tryDetail)(0);
        }); // httpGetJson
    }; // searchAt
    (*searchAt)(0);
}

void AnimetkaClient::getEpisodeStream(const QString &translationId,
                                      int episode,
                                      const QString &qualityHint,
                                      StreamCallback callback) {
    QString tid;
    QString qFromId;
    if (!parseTranslationId(translationId, &tid, &qFromId)) {
        callback({}, QStringLiteral("Animetka: некорректный id озвучки"));
        return;
    }
    QString quality = !qualityHint.isEmpty() ? qualityHint : qFromId;
    if (quality == QLatin1String("best"))
        quality.clear();

    QString materialId = m_materialId;
    if (m_tidToMaterial.contains(tid))
        materialId = m_tidToMaterial.value(tid).toString();
    if (materialId.isEmpty()) {
        callback({}, QStringLiteral("Animetka: material id неизвестен — сначала загрузи озвучки"));
        return;
    }

    QUrl pu(kBase + QStringLiteral("/api/anime/playlist"));
    QUrlQuery pq;
    pq.addQueryItem(QStringLiteral("material"), materialId);
    pq.addQueryItem(QStringLiteral("tid"), tid);
    pu.setQuery(pq);

    QPointer<AnimetkaClient> self(this);
    httpGetJson(pu, [self, episode, quality, callback, tid](QString body, QString err) {
        if (!self) {
            callback({}, QStringLiteral("cancelled"));
            return;
        }
        if (!err.isEmpty()) {
            callback({}, err);
            return;
        }
        const QJsonObject pl = QJsonDocument::fromJson(body.toUtf8()).object();
        const QJsonArray list = pl.value(QStringLiteral("list")).toArray();

        // Anilibria mapping: list пустой, но есть hls_* по ordinal
        QString mappedUrl;
        {
            const QJsonObject mapObj = pl.value(QStringLiteral("AnilibriaMapping")).toObject();
            const QJsonArray eps = mapObj.value(QStringLiteral("episodes")).toArray();
            QJsonObject hit;
            for (const QJsonValue &v : eps) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("ordinal")).toInt() == episode
                    || o.value(QStringLiteral("sort_order")).toInt() == episode) {
                    hit = o;
                    break;
                }
            }
            if (hit.isEmpty() && episode >= 1 && episode <= eps.size())
                hit = eps.at(episode - 1).toObject();
            if (!hit.isEmpty()) {
                const int want = quality.trimmed().toInt();
                const QList<QPair<int, QString>> fields = {
                    {1080, QStringLiteral("hls_1080")},
                    {720, QStringLiteral("hls_720")},
                    {480, QStringLiteral("hls_480")},
                };
                int bestQ = -1;
                for (const auto &f : fields) {
                    const QString u = hit.value(f.second).toString().trimmed();
                    if (u.isEmpty() || !isDirectPlayable(u))
                        continue;
                    if (want > 0) {
                        if (f.first == want) {
                            mappedUrl = u;
                            break;
                        }
                        if (f.first <= want && f.first > bestQ) {
                            bestQ = f.first;
                            mappedUrl = u;
                        }
                    } else if (f.first > bestQ) {
                        bestQ = f.first;
                        mappedUrl = u;
                    }
                }
            }
        }

        if (list.isEmpty()) {
            if (!mappedUrl.isEmpty()) {
                qInfo("Animetka: AnilibriaMapping tid=%s ep=%d host=%s",
                      qUtf8Printable(tid), episode, qUtf8Printable(QUrl(mappedUrl).host()));
                callback(mappedUrl, {});
                return;
            }
            callback({}, QStringLiteral("Animetka: плейлист пуст"));
            return;
        }

        QJsonObject episodeObj;
        const QString wantId = QStringLiteral("s%1").arg(episode);
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("id")).toString();
            const QString pjs = o.value(QStringLiteral("pjs_id")).toString();
            if (id == wantId || pjs == wantId) {
                episodeObj = o;
                break;
            }
            if (o.value(QStringLiteral("value")).toInt() == episode) {
                episodeObj = o;
                break;
            }
        }
        if (episodeObj.isEmpty() && episode >= 1 && episode <= list.size())
            episodeObj = list.at(episode - 1).toObject();
        if (episodeObj.isEmpty()) {
            if (!mappedUrl.isEmpty()) {
                callback(mappedUrl, {});
                return;
            }
            callback({}, QStringLiteral("Animetka: серия %1 не найдена").arg(episode));
            return;
        }

        const QString file = episodeObj.value(QStringLiteral("file")).toString();
        const QMap<int, QString> qmap = parseQualityFileField(file);
        pickUrlFromQualities(qmap, quality, [callback, episode, tid, mappedUrl](QString url, QString e2) {
            if (!e2.isEmpty() && e2 != QLatin1String("kodik_embed")) {
                callback({}, e2);
                return;
            }
            // animetka.com/video/...?code=//kodikplayer.com — готовый m3u8
            if (isDirectPlayable(url)) {
                qInfo("Animetka: stream tid=%s ep=%d host=%s",
                      qUtf8Printable(tid), episode, qUtf8Printable(QUrl(url).host()));
                callback(url, {});
                return;
            }
            if (!mappedUrl.isEmpty()) {
                callback(mappedUrl, {});
                return;
            }
            if (url.isEmpty() || e2 == QLatin1String("kodik_embed")
                || (url.contains(QStringLiteral("kodikplayer.com"))
                    && !url.contains(QStringLiteral("/video/")))) {
                callback(QStringLiteral("kodik_fallback:") + tid, {});
                return;
            }
            callback({}, QStringLiteral("Animetka: нет URL для серии %1").arg(episode));
        });
    });
}
