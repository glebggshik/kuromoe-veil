#include "HentaiSiteCommon.h"

#include <algorithm>

#include <QStringDecoder>
#include <QSet>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace HentaiSiteCommon {

namespace {

bool hasCyrillic(const QString &text) {
    for (const QChar ch : text) {
        if (ch.script() == QChar::Script_Cyrillic)
            return true;
    }
    return false;
}

bool hasCjk(const QString &text) {
    for (const QChar ch : text) {
        const QChar::Script script = ch.script();
        if (script == QChar::Script_Hiragana || script == QChar::Script_Katakana
            || script == QChar::Script_Han)
            return true;
    }
    return false;
}

bool isLatinTitle(const QString &text) {
    if (text.trimmed().isEmpty())
        return false;
    if (hasCyrillic(text) || hasCjk(text))
        return false;
    return true;
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

QString absolutePageUrl(const QString &mirrorBase, const QString &href) {
    const QString trimmed = href.trimmed();
    if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return trimmed;
    if (trimmed.startsWith(QLatin1Char('/')))
        return mirrorBase + trimmed;
    return mirrorBase + QLatin1Char('/') + trimmed;
}

QString voiceKeyFromUrl(const QString &url) {
    const QString lower = url.toLower();
    if (lower.contains(QStringLiteral("_sub_")) || lower.contains(QStringLiteral("-sub-"))
        || lower.contains(QStringLiteral("_sub.")) || lower.contains(QStringLiteral("суб")))
        return QStringLiteral("sub");
    if (lower.contains(QStringLiteral("_rus_")) || lower.contains(QStringLiteral("-rus-"))
        || lower.contains(QStringLiteral("anistar")) || lower.contains(QStringLiteral("озвуч")))
        return QStringLiteral("rus");
    return QStringLiteral("orig");
}

int episodeFromUrl(const QString &url) {
    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral(R"((?:^|[-_/])(\d{1,2})[_-](?:rus|sub|vo))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"([-_\s](\d{1,2})[_-](?:\d{4}|www|hentasis|anistar))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(серия\s*(\d{1,2}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:^|[^\d])(\d{1,2})(?:\.mp4|$))")),
    };
    const QString lower = url.toLower();
    for (const QRegularExpression &re : patterns) {
        const auto m = re.match(lower);
        if (m.hasMatch()) {
            const int ep = m.captured(1).toInt();
            if (ep > 0 && ep < 100)
                return ep;
        }
    }
    return 0;
}

QString voiceTitle(const QString &key) {
    if (key == QStringLiteral("rus"))
        return QStringLiteral("Русская озвучка");
    if (key == QStringLiteral("sub"))
        return QStringLiteral("Русские субтитры");
    return QStringLiteral("Оригинал");
}

bool isJunkDleLink(const QString &url, const QString &title) {
    const QString u = url.toLower();
    const QString t = title.toLower();
    if (u.contains(QStringLiteral("-help.")) || u.contains(QStringLiteral("/help")))
        return true;
    if (t.contains(QStringLiteral("есть свет в конце")) || t.contains(QStringLiteral("зависит от вас")))
        return true;
    if (t.contains(QStringLiteral("регистрац")) || t.contains(QStringLiteral("войти")))
        return true;
    return false;
}

} // namespace

QString htmlBodyToString(const QByteArray &body) {
    if (body.isEmpty())
        return {};

    QString charset;
    const QString head = QString::fromLatin1(body.left(4096));
    static const QRegularExpression charsetRe(
        QStringLiteral(R"re(charset\s*=\s*["']?([\w-]+))re"), QRegularExpression::CaseInsensitiveOption);
    const auto cm = charsetRe.match(head);
    if (cm.hasMatch())
        charset = cm.captured(1).trimmed().toLower();

    auto decodeWith = [&](const char *name) -> QString {
        QStringDecoder dec(name);
        return dec.decode(body);
    };

    if (charset == QStringLiteral("windows-1251") || charset == QStringLiteral("cp1251")
        || charset == QStringLiteral("x-cp1251"))
        return decodeWith("windows-1251");

    QString utf8 = decodeWith("UTF-8");
    QStringDecoder utf8Check("UTF-8");
    utf8Check.decode(body);
    if (!utf8Check.hasError())
        return utf8;

    return decodeWith("windows-1251");
}

namespace {

bool pageLooksAlive(const QByteArray &body) {
    const QString text = htmlBodyToString(body).left(8000).toLower();
    if (text.contains(QStringLiteral("cloudflare")) && text.contains(QStringLiteral("blocked")))
        return false;
    if (text.contains(QStringLiteral("attention required")))
        return false;
    return !body.isEmpty();
}

} // namespace

QString decodeHtmlEntities(const QString &text) {
    QString out = text;
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return out;
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

int scoreSearchField(const QString &field, const QStringList &queries) {
    const QString norm = normalizeSearchText(field);
    if (norm.isEmpty())
        return 0;
    int best = 0;
    for (const QString &query : queries) {
        const QString qn = normalizeSearchText(query);
        if (qn.isEmpty())
            continue;
        if (norm == qn) {
            best = qMax(best, 100);
            continue;
        }
        if (norm.contains(qn) || qn.contains(norm)) {
            best = qMax(best, 75);
            continue;
        }
        best = qMax(best, wordOverlapScore(norm, qn));
    }
    return best;
}

int scoreSearchHit(const HentaiSearchHit &hit, const QStringList &queries, int year) {
    int score = scoreSearchField(hit.title, queries);
    const QUrl pageUrl(hit.pageUrl);
    score = qMax(score, scoreSearchField(pageUrl.path(), queries));
    if (year > 0 && hit.title.contains(QString::number(year)))
        score += 8;
    return score;
}

QList<HentaiSearchHit> parseDleSearchHits(const QByteArray &html, const QString &mirrorBase) {
    QList<HentaiSearchHit> out;
    const QString body = htmlBodyToString(html);
    static const QRegularExpression linkRes[] = {
        QRegularExpression(
            QStringLiteral(R"re(<a[^>]+href="([^"]*/\d+-[^"]+\.html)"[^>]*>([^<]+)</a>)re"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"re(<a[^>]+href="([^"]*/hentai/\d+-[^"]+\.html)"[^>]*>([^<]+)</a>)re"),
            QRegularExpression::CaseInsensitiveOption),
    };
    QSet<QString> seen;
    for (const QRegularExpression &linkRe : linkRes) {
        auto it = linkRe.globalMatch(body);
        while (it.hasNext()) {
            const auto m = it.next();
            QString title = decodeHtmlEntities(m.captured(2)).trimmed();
            title.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
            const QString pageUrl = absolutePageUrl(mirrorBase, m.captured(1));
            if (title.size() < 4 || seen.contains(pageUrl) || isJunkDleLink(pageUrl, title))
                continue;
            seen.insert(pageUrl);
            HentaiSearchHit hit;
            hit.title = title;
            hit.pageUrl = pageUrl;
            hit.mirrorBase = mirrorBase;
            out << hit;
        }
    }
    return out;
}

QList<HentaiVoiceTrack> parseMp4VoiceTracks(const QString &html) {
    static const QRegularExpression fileRe(
        QStringLiteral(R"re(file:\s*['"]([^'"]+)['"])re"), QRegularExpression::CaseInsensitiveOption);
    QHash<QString, HentaiVoiceTrack> byVoice;
    auto it = fileRe.globalMatch(html);
    while (it.hasNext()) {
        const QString url = decodeHtmlEntities(it.next().captured(1)).trimmed();
        if (!url.contains(QStringLiteral(".mp4"), Qt::CaseInsensitive))
            continue;
        const QString voiceKey = voiceKeyFromUrl(url);
        int episode = episodeFromUrl(url);
        if (episode <= 0) {
            const int count = byVoice.value(voiceKey).episodes;
            episode = count > 0 ? count + 1 : 1;
        }
        HentaiVoiceTrack &track = byVoice[voiceKey];
        if (track.id.isEmpty()) {
            track.id = voiceKey;
            track.title = voiceTitle(voiceKey);
        }
        track.episodeUrls[episode] = url;
        track.episodes = qMax(track.episodes, episode);
    }

    QList<HentaiVoiceTrack> out;
    const QStringList order = {QStringLiteral("rus"), QStringLiteral("sub"), QStringLiteral("orig")};
    for (const QString &key : order) {
        if (byVoice.contains(key))
            out << byVoice.value(key);
    }
    for (auto it = byVoice.constBegin(); it != byVoice.constEnd(); ++it) {
        if (!order.contains(it.key()))
            out << it.value();
    }
    return out;
}

QStringList buildSearchQueries(const QString &title, const QString &originalTitle,
                               const QString &englishTitle, const QString &japaneseTitle) {
    QStringList queries;
    QSet<QString> seen;
    auto add = [&](const QString &q) {
        const QString trimmed = q.trimmed();
        if (trimmed.size() < 4)
            return;
        const QString key = trimmed.toLower();
        if (seen.contains(key))
            return;
        seen.insert(key);
        queries << trimmed;
    };

    if (hasCyrillic(title))
        add(title);
    if (!englishTitle.isEmpty() && isLatinTitle(englishTitle))
        add(englishTitle);
    if (!originalTitle.isEmpty() && isLatinTitle(originalTitle)
        && originalTitle.compare(englishTitle, Qt::CaseInsensitive) != 0)
        add(originalTitle);
    if (!japaneseTitle.isEmpty() && hasCjk(japaneseTitle))
        add(japaneseTitle);

    const int colon = title.indexOf(QLatin1Char(':'));
    if (colon > 0)
        add(title.left(colon).trimmed());
    const int comma = title.indexOf(QLatin1Char(','));
    if (comma > 0)
        add(title.left(comma).trimmed());
    return queries;
}

QStringList parseMirrorsFromHtml(const QByteArray &html) {
    const QString body = htmlBodyToString(html);
    QStringList out;
    QSet<QString> seen;

    auto toMirrorBase = [](const QString &raw) -> QString {
        QString url = raw.trimmed();
        if (url.isEmpty())
            return {};
        if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            && !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
            url = QStringLiteral("https://") + url;
        const QUrl parsed(url);
        if (!parsed.isValid() || parsed.host().isEmpty())
            return {};
        return parsed.scheme() + QStringLiteral("://") + parsed.authority();
    };

    auto add = [&](const QString &raw, bool prepend = false) {
        const QString base = toMirrorBase(raw);
        if (base.isEmpty() || seen.contains(base))
            return;
        seen.insert(base);
        if (prepend)
            out.prepend(base);
        else
            out << base;
    };

    // AniStar: блок «Актуальный адрес сайта AniStar» — <a class="act" href="/as_act.php">V28.ASTAR.BZ</a>
    static const QRegularExpression anistarActPatterns[] = {
        QRegularExpression(
            QStringLiteral(
                R"re(<a[^>]+class=["']act["'][^>]*href=["'][^"']*as_act\.php[^"']*["'][^>]*>([^<]+)</a>)re"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(
                R"re(<a[^>]+href=["'][^"']*as_act\.php[^"']*["'][^>]+class=["']act["'][^>]*>([^<]+)</a>)re"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"re(Актуальный адрес сайта\s+AniStar[\s\S]{0,500}?<a[^>]+>([Vv]\d+\.astar\.bz)</a>)re"),
            QRegularExpression::CaseInsensitiveOption),
    };
    for (const QRegularExpression &re : anistarActPatterns) {
        auto it = re.globalMatch(body);
        while (it.hasNext())
            add(it.next().captured(1), true);
    }

    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral(R"re(<base[^>]+href=["']([^"']+)["'])re"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"re(<link[^>]+rel=["']canonical["'][^>]+href=["']([^"']+)["'])re"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"re(<link[^>]+href=["']([^"']+)["'][^>]+rel=["']canonical["'])re"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"re((https?://v\d+\.(?:astar\.bz|hentasis\d*\.top|hentasis\.me)[^"'\s<]*))re"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"re(\b([Vv]\d+\.astar\.bz)\b)re"), QRegularExpression::CaseInsensitiveOption),
    };
    for (const QRegularExpression &re : patterns) {
        auto it = re.globalMatch(body);
        while (it.hasNext())
            add(it.next().captured(1));
    }
    return out;
}

QStringList mergeMirrorLists(const QStringList &preferred, const QStringList &extra) {
    QStringList out;
    QSet<QString> seen;
    for (const QString &mirror : preferred + extra) {
        const QString trimmed = mirror.trimmed();
        if (trimmed.isEmpty() || seen.contains(trimmed))
            continue;
        seen.insert(trimmed);
        out << trimmed;
    }
    return out;
}

void discoverMirrors(const QStringList &seeds, const QStringList &fallback, bool useProxy,
                     HentaiMirrorsCallback callback) {
    struct State {
        QStringList seeds;
        QStringList fallback;
        bool useProxy = false;
        HentaiMirrorsCallback callback;
        QStringList discovered;
        int pending = 0;
        bool finished = false;
    };
    auto state = std::make_shared<State>();
    state->seeds = seeds;
    state->fallback = fallback;
    state->useProxy = useProxy;
    state->callback = std::move(callback);

    auto finish = [state]() {
        if (state->finished)
            return;
        state->finished = true;
        state->callback(mergeMirrorLists(state->discovered, state->fallback));
    };

    if (state->seeds.isEmpty()) {
        finish();
        return;
    }

    state->pending = state->seeds.size();
    for (const QString &seed : state->seeds) {
        QString url = seed.trimmed();
        if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            && !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
            url = QStringLiteral("https://") + url;

        QNetworkRequest netRequest{QUrl(url)};
        netRequest.setHeader(QNetworkRequest::UserAgentHeader,
                             QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
        netRequest.setRawHeader("Accept", "text/html,application/xhtml+xml");
        netRequest.setRawHeader("Accept-Language", "ru-RU,ru;q=0.9,en;q=0.8");

        QNetworkReply *reply = nullptr;
        if (state->useProxy)
            reply = NetworkManager::instance()->get(netRequest);
        else
            reply = NetworkManager::instance()->getLocal(netRequest);

        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, state, finish]() {
            const QByteArray body = reply->readAll();
            const QUrl finalUrl = reply->url();
            reply->deleteLater();

            const QStringList parsed = parseMirrorsFromHtml(body);
            QStringList chunk = parsed;
            if (!finalUrl.authority().isEmpty()) {
                const QString mirrorBase = finalUrl.scheme() + QStringLiteral("://") + finalUrl.authority();
                chunk = mergeMirrorLists(parsed, {mirrorBase});
            }
            state->discovered = mergeMirrorLists(chunk, state->discovered);

            if (--state->pending <= 0)
                finish();
        });
    }
}

void fetchFirstMirror(const QStringList &mirrors, const QString &path, bool useProxy,
                      HentaiBytesCallback callback) {
    struct State {
        QStringList mirrors;
        QString path;
        bool useProxy = false;
        HentaiBytesCallback callback;
        int index = 0;
    };
    auto state = std::make_shared<State>();
    state->mirrors = mirrors;
    state->path = path;
    state->useProxy = useProxy;
    state->callback = std::move(callback);

    auto runAt = std::make_shared<std::function<void()>>();
    *runAt = [state, runAt]() {
        if (state->index >= state->mirrors.size()) {
            state->callback({}, QString(), QStringLiteral("Все зеркала недоступны"));
            return;
        }

        const QString base = state->mirrors.at(state->index).trimmed();
        ++state->index;
        QUrl url(base + state->path);
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                         "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
        request.setRawHeader("Accept", "text/html,application/xhtml+xml");
        request.setRawHeader("Accept-Language", "ru-RU,ru;q=0.9,en;q=0.8");

        QNetworkReply *reply = state->useProxy
            ? NetworkManager::instance()->get(request)
            : NetworkManager::instance()->getLocal(request);

        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, state, runAt, base]() {
            const QByteArray body = reply->readAll();
            const auto err = reply->error();
            const QUrl finalUrl = reply->url();
            reply->deleteLater();

            QString mirrorBase = finalUrl.scheme() + QStringLiteral("://") + finalUrl.authority();
            if (mirrorBase.isEmpty())
                mirrorBase = base;

            if (err == QNetworkReply::NoError && pageLooksAlive(body)) {
                state->callback(body, mirrorBase, QString());
                return;
            }
            (*runAt)();
        });
    };
    (*runAt)();
}

QVariantList tracksToTranslations(const QList<HentaiVoiceTrack> &tracks, const QString &idPrefix,
                                  const QString &labelPrefix) {
    QVariantList out;
    for (const HentaiVoiceTrack &track : tracks) {
        if (track.episodes <= 0)
            continue;
        QVariantMap row;
        row[QStringLiteral("id")] = idPrefix + track.id;
        row[QStringLiteral("title")] = labelPrefix + track.title;
        row[QStringLiteral("episodes")] = track.episodes;
        out << row;
    }
    return out;
}

} // namespace HentaiSiteCommon