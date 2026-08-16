#include "KodikClient.h"

#include <algorithm>
#include <memory>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "AppConfig.h"
#include "NetworkManager.h"

namespace {

const QString kTestShikimoriId = QStringLiteral("20");
const QString kTokensUrl =
    QStringLiteral("https://raw.githubusercontent.com/YaNesyTortiK/AnimeParsers/refs/heads/main/kdk_tokns/tokens.json");
const QString kLegacyScriptUrl = QStringLiteral("https://kodik-add.com/add-players.min.js?v=2");

using VoidCallback = std::function<void(QString error)>;
using StringCallback = std::function<void(QString result, QString error)>;

QString reverseString(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (int i = s.size() - 1; i >= 0; --i)
        out.append(s.at(i));
    return out;
}

QString decryptToken(const QString &tkn) {
    const int half = tkn.size() / 2;
    auto decodePart = [](const QString &part) {
        return QByteArray::fromBase64(part.toUtf8().constData());
    };
    const QByteArray p1 = decodePart(reverseString(tkn.left(half)));
    const QByteArray p2 = decodePart(reverseString(tkn.mid(half)));
    return QString::fromUtf8(p2 + p1);
}

QPair<int, int> parseSeriesRange(const QString &name) {
    static const QRegularExpression re(QStringLiteral("\\((\\d+)~?-?(\\d*) эп\\.\\)"));
    auto m = re.match(name);
    if (!m.hasMatch())
        return {0, 0};
    const int first = m.captured(1).toInt();
    const QString second = m.captured(2);
    if (second.isEmpty())
        return {1, first};
    return {first, second.toInt()};
}

bool isSerialLink(const QString &link) {
    const int idx = link.indexOf(QStringLiteral("kodikplayer.com/"));
    return idx >= 0 && idx + 16 < link.size() && link.at(idx + 16) == QLatin1Char('s');
}

QString attrValue(const QString &attrs, const QString &name) {
    QRegularExpression re(name + QStringLiteral("=\"([^\"]*)\""));
    auto m = re.match(attrs);
    if (m.hasMatch())
        return m.captured(1);
    re.setPattern(name + QStringLiteral("='([^']*)'"));
    m = re.match(attrs);
    return m.hasMatch() ? m.captured(1) : QString();
}

bool optionMatchesTranslation(const QString &attrs, const QString &translationId) {
    return attrValue(attrs, QStringLiteral("value")) == translationId
        || attrValue(attrs, QStringLiteral("data-id")) == translationId;
}

QString scriptSrcAtTagIndex(const QString &html, int /*tagIndex*/) {
    // 2026: serial → app.serial.js, seria → app.player_single.js, film → app.video.js.
    // Индекс <script>[1] ненадёжен (adsbygoogle.js на части страниц).
    static const QRegularExpression appRe(
        QStringLiteral("src=\"(/assets/js/app\\.(?:serial|video|player_single)\\.[a-f0-9]+\\.js)\""));
    auto m = appRe.match(html);
    if (m.hasMatch())
        return m.captured(1);
    static const QRegularExpression scriptRe(QStringLiteral("<script([^>]*)>([\\s\\S]*?)</script>"));
    auto it = scriptRe.globalMatch(html);
    while (it.hasNext()) {
        const QString src = attrValue(it.next().captured(1), QStringLiteral("src"));
        if (src.contains(QStringLiteral("/assets/js/app.")))
            return src;
    }
    return {};
}

QString selectBlockInTranslationsBox(const QString &html, bool serial) {
    const QString marker = serial ? QStringLiteral("serial-translations-box")
                                  : QStringLiteral("movie-translations-box");
    // Класс встречается десятки раз в CSS; нужен именно div плеера:
    // ...serial-translations-box"> <select>
    static const QRegularExpression re(
        QStringLiteral("%1\"[^>]*>\\s*<select([\\s\\S]*?)</select>").arg(marker));
    auto m = re.match(html);
    if (!m.hasMatch())
        return {};
    return QStringLiteral("<select") + m.captured(1) + QStringLiteral("</select>");
}

bool parseMediaFromPlayerLink(const QString &link, QString *mediaId, QString *mediaHash) {
    static const QRegularExpression re(
        QStringLiteral("kodikplayer\\.com/(?:serial|video)/(\\d+)/([a-f0-9]+)/"));
    auto m = re.match(link);
    if (!m.hasMatch())
        return false;
    if (mediaId)
        *mediaId = m.captured(1);
    if (mediaHash)
        *mediaHash = m.captured(2);
    return true;
}

QString inlineScriptAtTagIndex(const QString &html, int tagIndex) {
    static const QRegularExpression scriptRe(QStringLiteral("<script([^>]*)>([\\s\\S]*?)</script>"));
    int idx = 0;
    auto it = scriptRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        if (idx == tagIndex)
            return m.captured(2);
        ++idx;
    }
    return {};
}

QVariantList parseTranslationsHtml(const QString &html, bool serial) {
    QVariantList out;
    const QString selectBlock = selectBlockInTranslationsBox(html, serial);
    if (selectBlock.isEmpty())
        return out;
    static const QRegularExpression optionRe(QStringLiteral("<option\\s+([^>]+)>([^<]*)</option>"));
    auto it = optionRe.globalMatch(selectBlock);
    while (it.hasNext()) {
        auto m = it.next();
        const QString attrs = m.captured(1);
        QString name = m.captured(2).trimmed();
        const QString id = attrValue(attrs, QStringLiteral("value"));
        QString type = attrValue(attrs, QStringLiteral("data-translation-type"));
        if (type == QLatin1String("voice"))
            type = QStringLiteral("Озвучка");
        else if (type == QLatin1String("subtitles"))
            type = QStringLiteral("Субтитры");
        if (id.isEmpty() || name.isEmpty())
            continue;
        // Селектор сезонов (Сезон 1 / Спешл) не помечен как озвучка — отсекаем.
        if (type.isEmpty())
            continue;
        const auto range = parseSeriesRange(name);
        QVariantMap row;
        row[QStringLiteral("id")] = id;
        row[QStringLiteral("name")] = name;
        row[QStringLiteral("type")] = type;
        row[QStringLiteral("episodes")] = range.second > 0 ? range.second : 0;
        out << row;
    }
    return out;
}

QVariantList parseTranslationsApi(const QJsonArray &results) {
    QMap<QString, QVariantMap> byId;
    for (const QJsonValue &rv : results) {
        const QJsonObject r = rv.toObject();
        const QJsonObject tr = r.value(QStringLiteral("translation")).toObject();
        const QString id = QString::number(tr.value(QStringLiteral("id")).toInt());
        if (id.isEmpty() || id == QLatin1String("0"))
            continue;
        const QString studio = tr.value(QStringLiteral("title")).toString();
        if (studio.isEmpty())
            continue;
        const QString trType = tr.value(QStringLiteral("type")).toString();
        int ep = r.value(QStringLiteral("last_episode")).toInt();
        if (ep <= 0)
            ep = r.value(QStringLiteral("episodes_count")).toInt();

        const QVariantMap existing = byId.value(id);
        if (!existing.isEmpty() && existing.value(QStringLiteral("episodes")).toInt() >= ep)
            continue;

        QString typeLabel;
        if (trType == QLatin1String("voice"))
            typeLabel = QStringLiteral("Озвучка");
        else if (trType == QLatin1String("subtitles"))
            typeLabel = QStringLiteral("Субтитры");
        else
            typeLabel = trType;

        QString displayName = studio;
        if (ep > 0)
            displayName += QStringLiteral(" (") + QString::number(ep) + QStringLiteral(" сер.)");

        QVariantMap row;
        row[QStringLiteral("id")] = id;
        row[QStringLiteral("name")] = displayName;
        row[QStringLiteral("episodes")] = ep;
        row[QStringLiteral("type")] = typeLabel;
        byId[id] = row;
    }

    QVariantList list;
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
        list << it.value();
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap am = a.toMap();
        const QVariantMap bm = b.toMap();
        const bool aVoice = am.value(QStringLiteral("type")) == QStringLiteral("Озвучка");
        const bool bVoice = bm.value(QStringLiteral("type")) == QStringLiteral("Озвучка");
        if (aVoice != bVoice)
            return aVoice > bVoice;
        return am.value(QStringLiteral("name")).toString().localeAwareCompare(
                   bm.value(QStringLiteral("name")).toString()) < 0;
    });
    return list;
}

QString extractJsonObjectAfter(const QString &text, const QString &marker) {
    const int start = text.indexOf(marker);
    if (start < 0)
        return {};
    int brace = text.indexOf(QLatin1Char('{'), start);
    if (brace < 0)
        return {};
    int depth = 0;
    for (int i = brace; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return text.mid(brace, i - brace + 1);
        }
    }
    return {};
}

QString extractQuotedAfter(const QString &text, const QString &marker) {
    const int idx = text.indexOf(marker);
    if (idx < 0)
        return {};
    const int start = idx + marker.size();
    const int end = text.indexOf(QLatin1Char('\''), start);
    if (end < 0)
        return {};
    return text.mid(start, end - start);
}

QString hashContainerFromHtml(const QString &html) {
    const QString inline4 = inlineScriptAtTagIndex(html, 4);
    if (inline4.contains(QStringLiteral(".type = '")))
        return inline4;
    static const QRegularExpression scriptRe(QStringLiteral("<script([^>]*)>([\\s\\S]*?)</script>"));
    auto it = scriptRe.globalMatch(html);
    while (it.hasNext()) {
        const QString body = it.next().captured(2);
        if (body.contains(QStringLiteral(".type = '")))
            return body;
    }
    return {};
}

bool isValidPostLink(const QString &path) {
    return path.startsWith(QLatin1Char('/')) && path.size() >= 4;
}

QString decodeB64Path(const QString &encoded) {
    const QByteArray decoded = QByteArray::fromBase64(encoded.toLatin1());
    const QString path = QString::fromUtf8(decoded);
    return isValidPostLink(path) ? path : QString();
}

QString extractPostLinkFromScript(const QString &script) {
    // Новый Kodik (2026): $.ajax({...,url:atob("L2Z0b3I="),cache:!1,...}) → /ftor
    static const QRegularExpression atobRe(
        QStringLiteral(R"(atob\s*\(\s*\"([A-Za-z0-9+/=]+)\"\s*\))"));
    auto atobIt = atobRe.globalMatch(script);
    while (atobIt.hasNext()) {
        const QString path = decodeB64Path(atobIt.next().captured(1));
        if (!path.isEmpty())
            return path;
    }

    const int ajaxPos = script.indexOf(QStringLiteral("$.ajax"));
    if (ajaxPos < 0)
        return {};
    const int cachePos = script.indexOf(QStringLiteral("cache:!1"), ajaxPos);
    if (cachePos > ajaxPos) {
        const QString path = decodeB64Path(script.mid(ajaxPos + 30, cachePos - ajaxPos - 33));
        if (!path.isEmpty())
            return path;
    }
    static const QRegularExpression quotedB64(
        QStringLiteral(R"(\$\.ajax\(\{[^}]*\},\s*\"([A-Za-z0-9+/=]{8,})\"\s*,)"));
    auto m = quotedB64.match(script, ajaxPos);
    if (m.hasMatch())
        return decodeB64Path(m.captured(1));

    if (script.contains(QStringLiteral("L2Z0b3I=")))
        return QStringLiteral("/ftor");

    return {};
}

bool findEpisodeInSeriesBox(const QString &html, int episode, QString *epId, QString *epHash) {
    const int boxPos = html.indexOf(QStringLiteral("serial-series-box"));
    if (boxPos < 0)
        return false;
    const int selectPos = html.indexOf(QStringLiteral("<select"), boxPos);
    const int selectEnd = html.indexOf(QStringLiteral("</select>"), selectPos);
    if (selectPos < 0 || selectEnd < 0)
        return false;
    const QString block = html.mid(selectPos, selectEnd - selectPos);
    static const QRegularExpression optionRe(QStringLiteral("<option\\s+([^>]+)>"));
    const QString epStr = QString::number(episode);
    auto it = optionRe.globalMatch(block);
    while (it.hasNext()) {
        const QString attrs = it.next().captured(1);
        if (attrValue(attrs, QStringLiteral("value")) != epStr)
            continue;
        QString id = attrValue(attrs, QStringLiteral("data-id"));
        if (id.isEmpty())
            id = attrValue(attrs, QStringLiteral("data-serial-id"));
        QString hash = attrValue(attrs, QStringLiteral("data-hash"));
        if (hash.isEmpty())
            hash = attrValue(attrs, QStringLiteral("data-serial-hash"));
        if (id.isEmpty() || hash.isEmpty())
            continue;
        if (epId)
            *epId = id;
        if (epHash)
            *epHash = hash;
        return true;
    }
    return false;
}

QString extractInlineVar(const QString &html, const QString &name) {
    // (?<![A-Za-z0-9_]) — граница слова слева: без неё "ref=" подстрокой
    // матчится внутри "href=" (h-REF-=), и вместо настоящего urlParams.ref
    // захватывается путь из <link href="/assets/images/favicon.png"> —
    // ref_sign после этого подписан под другим URL, и Kodik стабильно
    // отдаёт 500 на POST /ftor (баг был не на стороне Kodik/DDoS-Guard).
    const QRegularExpression re(
        QStringLiteral("(?<![A-Za-z0-9_])(?:var\\s+)?%1\\s*=\\s*\"([^\"]*)\"")
            .arg(QRegularExpression::escape(name)));
    auto m = re.match(html);
    return m.hasMatch() ? m.captured(1) : QString();
}

struct KodikSignFields {
    QString d;
    QString d_sign;
    QString pd;
    QString pd_sign;
    QString ref;
    QString ref_sign;
};

KodikSignFields signFieldsFromPage(const QString &html, const QJsonObject &urlParams) {
    KodikSignFields s;
    s.d = extractInlineVar(html, QStringLiteral("domain"));
    if (s.d.isEmpty())
        s.d = urlParams.value(QStringLiteral("d")).toString();
    s.d_sign = extractInlineVar(html, QStringLiteral("d_sign"));
    if (s.d_sign.isEmpty())
        s.d_sign = urlParams.value(QStringLiteral("d_sign")).toString();
    s.pd = extractInlineVar(html, QStringLiteral("pd"));
    if (s.pd.isEmpty())
        s.pd = urlParams.value(QStringLiteral("pd")).toString();
    s.pd_sign = extractInlineVar(html, QStringLiteral("pd_sign"));
    if (s.pd_sign.isEmpty())
        s.pd_sign = urlParams.value(QStringLiteral("pd_sign")).toString();
    s.ref = extractInlineVar(html, QStringLiteral("ref"));
    if (s.ref.isEmpty()) {
        const QString encoded = urlParams.value(QStringLiteral("ref")).toString();
        s.ref = encoded.isEmpty() ? QString() : QUrl::fromPercentEncoding(encoded.toUtf8());
    }
    s.ref_sign = extractInlineVar(html, QStringLiteral("ref_sign"));
    if (s.ref_sign.isEmpty())
        s.ref_sign = urlParams.value(QStringLiteral("ref_sign")).toString();
    return s;
}

QChar rotChar(QChar ch, int rot) {
    static const QString alph = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    const bool lower = ch.isLower();
    const int idx = alph.indexOf(ch.toUpper());
    if (idx < 0)
        return ch;
    QChar out = alph.at((idx + rot) % alph.size());
    return lower ? out.toLower() : out;
}

QString decodeLinkString(const QString &encoded, int *cryptStep) {
    auto tryRot = [&](int rot) -> QString {
        QString crypted;
        crypted.reserve(encoded.size());
        for (const QChar ch : encoded)
            crypted.append(rotChar(ch, rot));
        const int pad = (4 - (crypted.size() % 4)) % 4;
        crypted.append(QString(pad, QLatin1Char('=')));
        const QByteArray decoded = QByteArray::fromBase64(crypted.toUtf8());
        const QString result = QString::fromUtf8(decoded);
        if (result.contains(QStringLiteral("mp4:hls:manifest")))
            return result;
        return {};
    };

    if (cryptStep && *cryptStep >= 0) {
        const QString hit = tryRot(*cryptStep);
        if (!hit.isEmpty())
            return hit;
    }
    for (int rot = 0; rot < 26; ++rot) {
        const QString hit = tryRot(rot);
        if (!hit.isEmpty()) {
            if (cryptStep)
                *cryptStep = rot;
            return hit;
        }
    }
    return {};
}

QString normalizeHttpsLink(const QString &link) {
    if (link.startsWith(QStringLiteral("https://")) || link.startsWith(QStringLiteral("http://")))
        return link;
    if (link.startsWith(QStringLiteral("//")))
        return QStringLiteral("https:") + link;
    return link;
}

QString episodeLinkFromSearchResult(const QJsonObject &result, int episode) {
    const QJsonObject seasons = result.value(QStringLiteral("seasons")).toObject();
    const QString epKey = QString::number(episode);
    for (auto it = seasons.begin(); it != seasons.end(); ++it) {
        const QJsonObject eps = it.value().toObject().value(QStringLiteral("episodes")).toObject();
        if (eps.contains(epKey)) {
            const QString link = eps.value(epKey).toString();
            if (!link.isEmpty())
                return normalizeHttpsLink(link);
        }
    }
    return {};
}

QUrlQuery kodikStreamForm(const KodikSignFields &signs, const QString &videoType, const QString &videoHash,
                            const QString &videoId) {
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("hash"), videoHash);
    form.addQueryItem(QStringLiteral("id"), videoId);
    form.addQueryItem(QStringLiteral("type"), videoType);
    form.addQueryItem(QStringLiteral("d"), signs.d);
    form.addQueryItem(QStringLiteral("d_sign"), signs.d_sign);
    form.addQueryItem(QStringLiteral("pd"), signs.pd);
    form.addQueryItem(QStringLiteral("pd_sign"), signs.pd_sign);
    form.addQueryItem(QStringLiteral("ref"), signs.ref);
    form.addQueryItem(QStringLiteral("ref_sign"), signs.ref_sign);
    form.addQueryItem(QStringLiteral("bad_user"), QStringLiteral("false"));
    form.addQueryItem(QStringLiteral("cdn_is_working"), QStringLiteral("true"));
    return form;
}

QString kodikPostBlockedMessage(int status, const QByteArray &body) {
    if (status == 500 && body.isEmpty())
        return QStringLiteral(
            "Kodik отклонил запрос видео (защита DDoS-Guard). "
            "Это не связано с прокси — сервер блокирует автоматические POST-запросы. "
            "Попробуй AniLibria или торренты.");
    if (status >= 500)
        return QStringLiteral("Kodik временно недоступен (HTTP %1)").arg(status);
    return {};
}

bool isRetryableKodikStatus(int status) {
    // 500 на POST /ftor чаще всего временная блокировка DDoS-Guard (видно по
    // логам: тот же запрос вручную секунду спустя через тот же прокси
    // отрабатывает нормально) — раньше 500 сразу сдавался без единой попытки
    // повтора, хотя 502/503/504 уже ретраились. С backoff (см. httpPostForm/
    // httpGet — 2с * номер попытки) есть реальный шанс проскочить временный бан.
    return status == 500 || status == 502 || status == 503 || status == 504;
}

bool isRetryableKodikError(QNetworkReply::NetworkError err) {
    return err == QNetworkReply::TimeoutError
        || err == QNetworkReply::TemporaryNetworkFailureError
        || err == QNetworkReply::UnknownNetworkError
        || err == QNetworkReply::ProtocolUnknownError
        || err == QNetworkReply::ContentNotFoundError
        || err == QNetworkReply::ServiceUnavailableError
        || err == QNetworkReply::ProxyConnectionClosedError
        || err == QNetworkReply::ProxyConnectionRefusedError
        || err == QNetworkReply::ProxyTimeoutError;
}

bool isRetryableKodikReply(QNetworkReply::NetworkError err, int status, const QString &errText) {
    if (isRetryableKodikStatus(status))
        return true;
    if (err != QNetworkReply::NoError && isRetryableKodikError(err))
        return true;
    const QString t = errText.toLower();
    return t.contains(QStringLiteral("gateway timeout"))
        || t.contains(QStringLiteral("504"))
        || t.contains(QStringLiteral("502"))
        || t.contains(QStringLiteral("503"))
        || t.contains(QStringLiteral("timed out"));
}

// Случайная пауза перед повтором/следующим запросом в цепочке — без неё
// ретраи идут с точностью до миллисекунды от расчётного backoff, что для
// антибота (DDoS-Guard) — характерный признак скрипта, а не браузера.
int jitterMs(int minMs, int maxMs) {
    return QRandomGenerator::global()->bounded(minMs, maxMs + 1);
}

void tuneKodikRequest(QNetworkRequest &req, const QString &referer, bool isAjaxPost = false) {
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    req.setRawHeader("sec-ch-ua", R"("Not_A Brand";v="8", "Chromium";v="120", "Google Chrome";v="120")");
    req.setRawHeader("sec-ch-ua-mobile", "?0");
    req.setRawHeader("sec-ch-ua-platform", "\"Windows\"");
    req.setRawHeader("Sec-Fetch-Site", referer.isEmpty() ? "none" : "same-origin");
    if (isAjaxPost) {
        req.setRawHeader("Accept", "application/json, text/javascript, */*; q=0.01");
        req.setRawHeader("Origin", "https://kodikplayer.com");
        req.setRawHeader("X-Requested-With", "XMLHttpRequest");
        req.setRawHeader("Sec-Fetch-Mode", "cors");
        req.setRawHeader("Sec-Fetch-Dest", "empty");
    } else {
        req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
        req.setRawHeader("Sec-Fetch-Mode", "navigate");
        req.setRawHeader("Sec-Fetch-Dest", "iframe");
    }
    req.setRawHeader("Accept-Language", "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(60000);
    if (!referer.isEmpty())
        req.setRawHeader("Referer", referer.toUtf8());
}

void httpGet(const QUrl &url, StringCallback callback, int retriesLeft = 6, const QString &referer = {}) {
    QNetworkRequest req(url);
    tuneKodikRequest(req, referer);
    QNetworkReply *reply = NetworkManager::instance()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, url, retriesLeft, referer]() {
                         const QByteArray body = reply->readAll();
                         const auto err = reply->error();
                         const QString errText = reply->errorString();
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         reply->deleteLater();

                         if (body.contains("Видео запрещено к просмотру в данной стране")) {
                             callback({}, QStringLiteral(
                                            "Kodik недоступен в регионе — включи SOCKS5-прокси в настройках"));
                             return;
                         }

                         const bool retry = retriesLeft > 0 && isRetryableKodikReply(err, status, errText);
                         if (retry) {
                             const int attempt = 7 - retriesLeft;
                             qWarning("Kodik: GET %s — retry %d (%s HTTP %d)",
                                      qUtf8Printable(url.toString()),
                                      attempt,
                                      qUtf8Printable(errText),
                                      status);
                             QTimer::singleShot(2000 * attempt + jitterMs(0, 600), [url, callback, retriesLeft, referer]() {
                                 httpGet(url, callback, retriesLeft - 1, referer);
                             });
                             return;
                         }

                         if (err != QNetworkReply::NoError) {
                             qWarning("Kodik: GET %s — %s", qUtf8Printable(url.toString()), qUtf8Printable(errText));
                             callback({}, errText);
                             return;
                         }
                         if (status >= 400) {
                             qWarning("Kodik: GET %s — HTTP %d", qUtf8Printable(url.toString()), status);
                             callback({}, QStringLiteral("HTTP %1").arg(status));
                             return;
                         }
                         callback(QString::fromUtf8(body), {});
                     });
}

void httpPostForm(const QUrl &url, const QUrlQuery &form, StringCallback callback, int retriesLeft = 5,
                  const QString &referer = {}) {
    const QByteArray body = form.query(QUrl::FullyEncoded).toUtf8();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    tuneKodikRequest(req, referer, true);
    QNetworkReply *reply = NetworkManager::instance()->post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, url, form, retriesLeft, referer]() {
                         const QByteArray data = reply->readAll();
                         const auto err = reply->error();
                         const QString errText = reply->errorString();
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         reply->deleteLater();

                         const bool retry = retriesLeft > 0 && isRetryableKodikReply(err, status, errText);
                         if (retry) {
                             const int attempt = 6 - retriesLeft;
                             qWarning("Kodik: POST %s — retry %d (%s HTTP %d)",
                                      qUtf8Printable(url.toString()),
                                      attempt,
                                      qUtf8Printable(errText),
                                      status);
                             QTimer::singleShot(2000 * attempt + jitterMs(0, 600), [url, form, callback, retriesLeft, referer]() {
                                 httpPostForm(url, form, callback, retriesLeft - 1, referer);
                             });
                             return;
                         }

                         if (err != QNetworkReply::NoError) {
                             qWarning("Kodik: POST %s — %s", qUtf8Printable(url.toString()), qUtf8Printable(errText));
                             callback(QString::fromUtf8(data), errText);
                             return;
                         }
                         if (status >= 400) {
                             qWarning("Kodik: POST %s — HTTP %d", qUtf8Printable(url.toString()), status);
                             const QString blocked = kodikPostBlockedMessage(status, data);
                             callback(QString::fromUtf8(data),
                                      blocked.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : blocked);
                             return;
                         }
                         callback(QString::fromUtf8(data), {});
                     });
}

void httpGetLocal(const QUrl &url, StringCallback callback, int retriesLeft = 2) {
    QNetworkRequest req(url);
    tuneKodikRequest(req, {});
    req.setTransferTimeout(120000);
    QNetworkReply *reply = NetworkManager::instance()->getLocal(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, url, retriesLeft]() {
                         const QByteArray body = reply->readAll();
                         const auto err = reply->error();
                         const QString errText = reply->errorString();
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         reply->deleteLater();

                         const bool retry = retriesLeft > 0 && isRetryableKodikReply(err, status, errText);
                         if (retry) {
                             QTimer::singleShot(1500, [url, callback, retriesLeft]() {
                                 httpGetLocal(url, callback, retriesLeft - 1);
                             });
                             return;
                         }

                         if (err != QNetworkReply::NoError) {
                             callback({}, errText);
                             return;
                         }
                         if (status >= 400) {
                             callback({}, QStringLiteral("HTTP %1").arg(status));
                             return;
                         }
                         callback(QString::fromUtf8(body), {});
                     });
}

QUrl kodikResolverRequestUrl(const QString &pageUrl) {
    QString base = AppConfig::instance()->kodikResolverUrl().trimmed();
    if (base.isEmpty())
        return {};
    QUrl url(base);
    if (!url.isValid() || url.scheme().isEmpty())
        url = QUrl(QStringLiteral("http://") + base);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), pageUrl);
    url.setQuery(query);
    return url;
}

void apiPost(const QString &endpoint, const QUrlQuery &payload, StringCallback callback) {
    QUrlQuery form = payload;
    httpPostForm(QUrl(QStringLiteral("https://kodik-api.com/") + endpoint), form, callback);
}

QString embedLinkFor(const QString &token, const QString &id, const QString &idType) {
    if (idType == QLatin1String("shikimori")) {
        return QStringLiteral(
                   "https://kodik-api.com/get-player?title=Player&hasPlayer=false&url=https%%3A%%2F%%2Fkodikdb.com%%2Ffind-player%%3FshikimoriID%%3D%1&token=%2&shikimoriID=%1")
            .arg(id, token);
    }
    return {};
}

class KodikSession : public QObject {
public:
    explicit KodikSession(QObject *parent = nullptr) : QObject(parent), m_cryptStep(-1) {}

    void ensureToken(VoidCallback done) {
        const QString cached = AppConfig::instance()->kodikToken();
        if (!cached.isEmpty()) {
            checkToken(cached, [this, done](bool ok) {
                if (ok) {
                    m_token = AppConfig::instance()->kodikToken();
                    done({});
                    return;
                }
                fetchToken(0, done);
            });
            return;
        }
        fetchToken(0, done);
    }

    void loadTranslationsFromEmbed(const QString &shikimoriId, KodikClient::TranslationsCallback callback) {
        getEmbedLink(shikimoriId, [callback](QString embed, QString e2) {
            if (embed.isEmpty()) {
                callback({}, e2);
                return;
            }
            httpGet(QUrl(embed), [callback, embed](QString html, QString e3) {
                if (html.isEmpty()) {
                    callback({}, e3);
                    return;
                }
                if (html.contains(QStringLiteral("Видео запрещено к просмотру в данной стране"))) {
                    callback({}, QStringLiteral(
                                   "Kodik недоступен в регионе — включи SOCKS5-прокси в настройках"));
                    return;
                }
                const bool serial = isSerialLink(embed);
                const QVariantList list = parseTranslationsHtml(html, serial);
                callback(list, {});
            });
        });
    }

    void loadTranslations(const QString &shikimoriId, KodikClient::TranslationsCallback callback) {
        ensureToken([this, shikimoriId, callback](QString err) {
            if (!err.isEmpty()) {
                callback({}, err);
                return;
            }
            QUrlQuery payload;
            payload.addQueryItem(QStringLiteral("token"), m_token);
            payload.addQueryItem(QStringLiteral("shikimori_id"), shikimoriId);
            payload.addQueryItem(QStringLiteral("with_material_data"), QStringLiteral("false"));
            payload.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
            apiPost(QStringLiteral("search"), payload, [this, shikimoriId, callback](QString body, QString e2) {
                if (body.isEmpty()) {
                    loadTranslationsFromEmbed(shikimoriId, callback);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
                if (obj.contains(QStringLiteral("error"))) {
                    loadTranslationsFromEmbed(shikimoriId, callback);
                    return;
                }
                if (obj.value(QStringLiteral("total")).toInt() == 0) {
                    callback({}, {});
                    return;
                }
                const QVariantList list = parseTranslationsApi(obj.value(QStringLiteral("results")).toArray());
                if (list.isEmpty())
                    loadTranslationsFromEmbed(shikimoriId, callback);
                else
                    callback(list, {});
            });
        });
    }

    void getEpisodeStream(
        const QString &shikimoriId, int episode, const QString &translationId,
        KodikClient::StreamCallback callback) {
        ensureToken([this, shikimoriId, episode, translationId, callback](QString err) {
            if (!err.isEmpty()) {
                callback({}, err);
                return;
            }
            if (translationId != QLatin1String("0") && !translationId.isEmpty()) {
                fetchEpisodePageLink(shikimoriId, episode, translationId,
                    [this, shikimoriId, episode, translationId, callback](QString episodeUrl, QString e1) {
                        if (!episodeUrl.isEmpty()) {
                            loadStreamFromEpisodeUrl(episodeUrl, callback);
                            return;
                        }
                        fetchTranslationPlayerLink(shikimoriId, translationId,
                            [this, shikimoriId, episode, translationId, callback, e1](QString playerLink, QString e2) {
                                if (!playerLink.isEmpty()) {
                                    resolveFromPlayerLink(playerLink, episode, callback);
                                    return;
                                }
                                getEmbedLink(shikimoriId, [this, episode, translationId, callback, e1, e2](QString embed, QString e3) {
                                    if (embed.isEmpty()) {
                                        const QString err = !e3.isEmpty() ? e3 : (!e2.isEmpty() ? e2 : e1);
                                        callback({}, err);
                                        return;
                                    }
                                    resolveFromEmbed(embed, episode, translationId, callback);
                                });
                            });
                    });
                return;
            }
            getEmbedLink(shikimoriId, [this, episode, translationId, callback](QString embed, QString e2) {
                if (embed.isEmpty()) {
                    callback({}, e2);
                    return;
                }
                resolveFromEmbed(embed, episode, translationId, callback);
            });
        });
    }

private:
    QString m_token;
    int m_cryptStep;
    QString m_cachedPostLinkScript;
    QString m_cachedPostLink;

    void checkToken(const QString &token, std::function<void(bool)> callback) {
        const QString saved = m_token;
        m_token = token;
        getEmbedLink(kTestShikimoriId, [this, token, saved, callback](QString embed, QString err) {
            if (!embed.isEmpty()) {
                m_token = token;
                callback(true);
                return;
            }
            Q_UNUSED(saved);
            if (err.contains(QStringLiteral("токен"), Qt::CaseInsensitive)
                || err.contains(QStringLiteral("token"), Qt::CaseInsensitive)) {
                callback(false);
                return;
            }
            // Любая другая ошибка (нет в базе, геоблок) — токен считаем рабочим,
            // как в video_source.py::_check_token.
            m_token = token;
            callback(true);
        });
    }

    void fetchToken(int groupIndex, VoidCallback done) {
        httpGet(QUrl(kTokensUrl), [this, groupIndex, done](QString body, QString err) {
            if (!body.isEmpty()) {
                const QJsonObject root = QJsonDocument::fromJson(body.toUtf8()).object();
                QStringList groups = {QStringLiteral("stable"), QStringLiteral("unstable")};
                if (groupIndex < groups.size()) {
                    const QJsonArray arr = root.value(groups.at(groupIndex)).toArray();
                    tryTokenAt(arr, 0, groupIndex, done);
                    return;
                }
                const QJsonArray legacy = root.value(QStringLiteral("legacy")).toArray();
                if (!legacy.isEmpty()) {
                    storeToken(decryptToken(legacy.at(0).toObject().value(QStringLiteral("tokn")).toString()));
                    done({});
                    return;
                }
            }
            fetchLegacyScript(done, err);
        });
    }

    void tryTokenAt(const QJsonArray &arr, int index, int groupIndex, VoidCallback done) {
        if (index >= arr.size()) {
            fetchToken(groupIndex + 1, done);
            return;
        }
        const QString candidate = decryptToken(arr.at(index).toObject().value(QStringLiteral("tokn")).toString());
        checkToken(candidate, [this, arr, index, groupIndex, done](bool ok) {
            if (ok) {
                storeToken(m_token);
                done({});
                return;
            }
            QTimer::singleShot(2000, this, [this, arr, index, groupIndex, done]() {
                tryTokenAt(arr, index + 1, groupIndex, done);
            });
        });
    }

    void fetchLegacyScript(VoidCallback done, const QString &prevErr) {
        httpGet(QUrl(kLegacyScriptUrl), [this, done, prevErr](QString script, QString err) {
            if (script.isEmpty()) {
                done(prevErr.isEmpty() ? err : prevErr);
                return;
            }
            const int pos = script.indexOf(QStringLiteral("token="));
            if (pos < 0) {
                done(QStringLiteral("Не удалось получить токен Kodik"));
                return;
            }
            const int start = pos + 7;
            const int end = script.indexOf(QLatin1Char('"'), start);
            storeToken(script.mid(start, end - start));
            done({});
        });
    }

    void storeToken(const QString &token) {
        m_token = token;
        AppConfig::instance()->setKodikToken(token);
    }

    void fetchTranslationPlayerLink(
        const QString &shikimoriId, const QString &translationId, StringCallback callback) {
        QUrlQuery payload;
        payload.addQueryItem(QStringLiteral("token"), m_token);
        payload.addQueryItem(QStringLiteral("shikimori_id"), shikimoriId);
        payload.addQueryItem(QStringLiteral("translation_id"), translationId);
        payload.addQueryItem(QStringLiteral("with_material_data"), QStringLiteral("false"));
        payload.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
        apiPost(QStringLiteral("search"), payload, [callback](QString body, QString err) {
            if (body.isEmpty()) {
                callback({}, err);
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
            if (obj.contains(QStringLiteral("error"))) {
                callback({}, obj.value(QStringLiteral("error")).toString());
                return;
            }
            const QJsonArray results = obj.value(QStringLiteral("results")).toArray();
            if (results.isEmpty()) {
                callback({}, QStringLiteral("Озвучка не найдена на Kodik"));
                return;
            }
            const QString link = normalizeHttpsLink(results.at(0).toObject().value(QStringLiteral("link")).toString());
            if (link.isEmpty()) {
                callback({}, QStringLiteral("Озвучка не найдена на Kodik"));
                return;
            }
            callback(link, {});
        });
    }

    void loadStreamFromEpisodeUrl(const QString &episodeUrl, KodikClient::StreamCallback callback) {
        auto loadDirect = [this, callback, episodeUrl]() {
            httpGet(QUrl(episodeUrl),
                    [this, callback, episodeUrl](QString page, QString pageErr) {
                        if (page.isEmpty()) {
                            callback({}, pageErr.isEmpty() ? QStringLiteral("Серия недоступна на Kodik")
                                                           : pageErr);
                            return;
                        }
                        fetchStreamFromPage(page, QJsonObject(), callback, episodeUrl);
                    },
                    4,
                    QStringLiteral("https://kodikplayer.com/"));
        };

        const QUrl resolverReq = kodikResolverRequestUrl(episodeUrl);
        if (!resolverReq.isValid()) {
            loadDirect();
            return;
        }

        qInfo("Kodik: sidecar %s", qUtf8Printable(resolverReq.toString(QUrl::RemoveQuery)));
        httpGetLocal(resolverReq, [callback, loadDirect](QString body, QString err) {
            if (!body.isEmpty()) {
                const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
                const QString streamUrl = normalizeHttpsLink(obj.value(QStringLiteral("url")).toString());
                if (!streamUrl.isEmpty()) {
                    callback(streamUrl, {});
                    return;
                }
                const QString resolverErr = obj.value(QStringLiteral("error")).toString();
                if (!resolverErr.isEmpty()) {
                    qWarning("Kodik: sidecar — %s (fallback /ftor)", qUtf8Printable(resolverErr));
                }
            } else if (!err.isEmpty()) {
                qWarning("Kodik: sidecar недоступен — %s (fallback /ftor)", qUtf8Printable(err));
            }
            loadDirect();
        });
    }

    void fetchEpisodePageLink(
        const QString &shikimoriId, int episode, const QString &translationId, StringCallback callback) {
        QUrlQuery payload;
        payload.addQueryItem(QStringLiteral("token"), m_token);
        payload.addQueryItem(QStringLiteral("shikimori_id"), shikimoriId);
        payload.addQueryItem(QStringLiteral("translation_id"), translationId);
        payload.addQueryItem(QStringLiteral("with_episodes"), QStringLiteral("true"));
        payload.addQueryItem(QStringLiteral("with_material_data"), QStringLiteral("false"));
        payload.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
        apiPost(QStringLiteral("search"), payload, [episode, callback](QString body, QString err) {
            if (body.isEmpty()) {
                callback({}, err);
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
            if (obj.contains(QStringLiteral("error"))) {
                callback({}, obj.value(QStringLiteral("error")).toString());
                return;
            }
            const QJsonArray results = obj.value(QStringLiteral("results")).toArray();
            if (results.isEmpty()) {
                callback({}, QStringLiteral("Озвучка не найдена на Kodik"));
                return;
            }
            const QString link = episodeLinkFromSearchResult(results.at(0).toObject(), episode);
            if (link.isEmpty()) {
                callback({}, QStringLiteral("Серия недоступна на Kodik"));
                return;
            }
            callback(link, {});
        });
    }

    void getEmbedLink(const QString &shikimoriId, StringCallback callback) {
        const QString url = embedLinkFor(m_token, shikimoriId, QStringLiteral("shikimori"));
        httpGet(QUrl(url), [callback](QString body, QString err) {
            if (body.isEmpty()) {
                callback({}, err);
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
            if (obj.contains(QStringLiteral("error"))) {
                callback({}, obj.value(QStringLiteral("error")).toString());
                return;
            }
            if (!obj.value(QStringLiteral("found")).toBool()) {
                callback({}, QStringLiteral("Нет в каталоге Kodik"));
                return;
            }
            callback(normalizeHttpsLink(obj.value(QStringLiteral("link")).toString()), {});
        });
    }

    void resolveFromPlayerLink(const QString &playerLink, int episode, KodikClient::StreamCallback callback) {
        QString mediaId;
        QString mediaHash;
        if (!parseMediaFromPlayerLink(playerLink, &mediaId, &mediaHash)) {
            callback({}, QStringLiteral("Не удалось разобрать ссылку Kodik"));
            return;
        }
        httpGet(QUrl(playerLink), [this, playerLink, mediaId, mediaHash, episode, callback](QString html, QString err) {
            if (html.isEmpty()) {
                callback({}, err);
                return;
            }
            if (html.contains(QStringLiteral("Видео запрещено к просмотру в данной стране"))) {
                callback({}, QStringLiteral(
                               "Kodik недоступен в регионе — включи SOCKS5-прокси в настройках"));
                return;
            }
            const QString urlParamsJson = extractJsonObjectAfter(html, QStringLiteral("urlParams"));
            if (urlParamsJson.isEmpty()) {
                callback({}, QStringLiteral("Не удалось разобрать страницу Kodik"));
                return;
            }
            const QJsonObject urlParams = QJsonDocument::fromJson(urlParamsJson.toUtf8()).object();

            QString epId;
            QString epHash;
            if (findEpisodeInSeriesBox(html, episode, &epId, &epHash)) {
                const QString epUrl = QStringLiteral(
                                          "https://kodikplayer.com/serial/%1/%2/720p?min_age=16&first_url=false")
                                          .arg(epId, epHash);
                httpGet(QUrl(epUrl),
                        [this, urlParams, callback, epUrl](QString page, QString pageErr) {
                            if (page.isEmpty()) {
                                callback({}, pageErr.isEmpty() ? QStringLiteral("Серия недоступна на Kodik")
                                                               : pageErr);
                                return;
                            }
                            fetchStreamFromPage(page, urlParams, callback, epUrl);
                        },
                        4,
                        playerLink);
                return;
            }

            fetchEpisodePage(isSerialLink(playerLink), mediaId, mediaHash, episode, urlParams, callback,
                             playerLink);
        });
    }

    void resolveFromEmbed(
        const QString &embed, int episode, const QString &translationId,
        KodikClient::StreamCallback callback) {
        httpGet(QUrl(embed), [this, embed, episode, translationId, callback](QString html, QString err) {
            if (html.isEmpty()) {
                callback({}, err);
                return;
            }
            if (html.contains(QStringLiteral("Видео запрещено к просмотру в данной стране"))) {
                callback({}, QStringLiteral(
                               "Kodik недоступен в регионе — включи SOCKS5-прокси в настройках"));
                return;
            }

            const QString urlParamsJson = extractJsonObjectAfter(html, QStringLiteral("urlParams"));
            if (urlParamsJson.isEmpty()) {
                callback({}, QStringLiteral("Не удалось разобрать страницу Kodik"));
                return;
            }
            const QJsonObject urlParams = QJsonDocument::fromJson(urlParamsJson.toUtf8()).object();

            const bool serial = isSerialLink(embed);
            const bool needsEpisodePage =
                translationId != QLatin1String("0")
                && (episode != 0 || (episode == 0 && serial));

            if (!needsEpisodePage) {
                fetchStreamFromPage(html, urlParams, callback, embed);
                return;
            }

            const QString selectBlock = selectBlockInTranslationsBox(html, serial);
            if (selectBlock.isEmpty()) {
                callback({}, QStringLiteral("Озвучка не найдена на Kodik"));
                return;
            }

            QString mediaHash;
            QString mediaId;
            static const QRegularExpression optionRe(QStringLiteral("<option\\s+([^>]+)>"));
            auto it = optionRe.globalMatch(selectBlock);
            while (it.hasNext()) {
                const QString attrs = it.next().captured(1);
                if (!optionMatchesTranslation(attrs, translationId))
                    continue;
                mediaHash = attrValue(attrs, QStringLiteral("data-media-hash"));
                mediaId = attrValue(attrs, QStringLiteral("data-media-id"));
                break;
            }
            if (mediaId.isEmpty() || mediaHash.isEmpty()) {
                callback({}, QStringLiteral("Озвучка не найдена на Kodik"));
                return;
            }

            fetchEpisodePage(serial, mediaId, mediaHash, episode, urlParams, callback, embed);
        });
    }

    void fetchEpisodePage(
        bool serial, const QString &mediaId, const QString &mediaHash, int episode,
        const QJsonObject &urlParams, KodikClient::StreamCallback callback,
        const QString &referer = QStringLiteral("https://kodikplayer.com/")) {
        const QString episodeUrl = serial
            ? QStringLiteral(
                  "https://kodikplayer.com/serial/%1/%2/720p?min_age=16&first_url=false&season=1&episode=%3")
                  .arg(mediaId, mediaHash)
                  .arg(episode)
            : QStringLiteral(
                  "https://kodikplayer.com/video/%1/%2/720p?min_age=16&first_url=false&season=1&episode=%3")
                  .arg(mediaId, mediaHash)
                  .arg(episode);
        httpGet(QUrl(episodeUrl),
                [this, urlParams, callback, episodeUrl](QString page, QString err) {
                    if (page.isEmpty()) {
                        callback({}, err.isEmpty() ? QStringLiteral("Серия недоступна на Kodik") : err);
                        return;
                    }
                    fetchStreamFromPage(page, urlParams, callback, episodeUrl);
                },
                4,
                referer);
    }

    void fetchStreamFromPage(const QString &html, const QJsonObject &parentUrlParams,
                             KodikClient::StreamCallback callback,
                             const QString &referer = QStringLiteral("https://kodikplayer.com/")) {
        if (html.isEmpty()) {
            callback({}, QStringLiteral("Пустой ответ Kodik"));
            return;
        }

        QJsonObject urlParams = parentUrlParams;
        const QString pageParamsJson = extractJsonObjectAfter(html, QStringLiteral("urlParams"));
        if (!pageParamsJson.isEmpty()) {
            const QJsonObject pageParams = QJsonDocument::fromJson(pageParamsJson.toUtf8()).object();
            if (pageParams.contains(QStringLiteral("d")) && pageParams.contains(QStringLiteral("d_sign")))
                urlParams = pageParams;
        }
        const KodikSignFields signs = signFieldsFromPage(html, urlParams);

        const QString scriptUrl = scriptSrcAtTagIndex(html, 1);
        if (scriptUrl.isEmpty()) {
            callback({}, QStringLiteral("Не найден скрипт Kodik"));
            return;
        }

        const QString hashContainer = hashContainerFromHtml(html);
        const QString videoType = extractQuotedAfter(hashContainer, QStringLiteral(".type = '"));
        const QString videoHash = extractQuotedAfter(hashContainer, QStringLiteral(".hash = '"));
        const QString videoId = extractQuotedAfter(hashContainer, QStringLiteral(".id = '"));
        if (videoType.isEmpty() || videoHash.isEmpty() || videoId.isEmpty()) {
            callback({}, QStringLiteral("Не удалось извлечь hash видео"));
            return;
        }

        auto finishStreamPost = [this, callback](const QString &body, const QString &err) {
            if (body.isEmpty()) {
                callback({}, err.isEmpty() ? QStringLiteral("Kodik отклонил запрос видео") : err);
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
            if (obj.contains(QStringLiteral("error"))) {
                callback({}, obj.value(QStringLiteral("error")).toString());
                return;
            }
            const QJsonObject links = obj.value(QStringLiteral("links")).toObject();
            int maxQuality = 0;
            for (auto it = links.begin(); it != links.end(); ++it) {
                const int q = it.key().toInt();
                if (q > maxQuality)
                    maxQuality = q;
            }
            const QJsonArray qArr = links.value(QString::number(maxQuality)).toArray();
            if (qArr.isEmpty()) {
                callback({}, QStringLiteral("Kodik не вернул ссылки на видео"));
                return;
            }
            QString src = qArr.at(0).toObject().value(QStringLiteral("src")).toString();
            if (!src.contains(QStringLiteral("mp4:hls:manifest")))
                src = decodeLinkString(src, &m_cryptStep);
            if (src.isEmpty()) {
                callback({}, QStringLiteral("Не удалось расшифровать ссылку Kodik"));
                return;
            }
            callback(normalizeHttpsLink(src), {});
        };

        auto submitStreamPost = [this, videoType, videoHash, videoId, signs, callback, referer,
                                 finishStreamPost](const QString &postLink) {
            if (!isValidPostLink(postLink)) {
                callback({}, QStringLiteral("Не удалось получить endpoint Kodik"));
                return;
            }
            const QUrl postUrl(QStringLiteral("https://kodikplayer.com") + postLink);
            const QUrlQuery form = kodikStreamForm(signs, videoType, videoHash, videoId);

            httpPostForm(postUrl, form,
                         [this, postUrl, finishStreamPost](QString body, QString err) {
                             if (body.isEmpty() || err.contains(QStringLiteral("500"))
                                 || err.contains(QStringLiteral("HTTP 5"))) {
                                 qWarning("Kodik: POST %s failed — %s", qUtf8Printable(postUrl.toString()),
                                          qUtf8Printable(err));
                                 m_cachedPostLink.clear();
                                 m_cachedPostLinkScript.clear();
                             }
                             finishStreamPost(body, err);
                         },
                         3,
                         referer);
        };

        auto postLinkReady = [this, scriptUrl, submitStreamPost, referer](const QString &postLink) {
            if (!isValidPostLink(postLink) && !scriptUrl.isEmpty()) {
                m_cachedPostLink.clear();
                m_cachedPostLinkScript.clear();
                httpGet(QUrl(QStringLiteral("https://kodikplayer.com") + scriptUrl),
                        [this, scriptUrl, submitStreamPost](QString script, QString) {
                            const QString fresh = extractPostLinkFromScript(script);
                            if (isValidPostLink(fresh)) {
                                m_cachedPostLinkScript = scriptUrl;
                                m_cachedPostLink = fresh;
                            }
                            submitStreamPost(fresh);
                        },
                        4,
                        referer);
                return;
            }
            submitStreamPost(postLink);
        };

        if (isValidPostLink(m_cachedPostLink) && m_cachedPostLinkScript == scriptUrl) {
            postLinkReady(m_cachedPostLink);
            return;
        }

        httpGet(QUrl(QStringLiteral("https://kodikplayer.com") + scriptUrl),
                [this, scriptUrl, postLinkReady](QString script, QString err) {
                    if (script.isEmpty()) {
                        qWarning("Kodik: app script empty %s — %s", qUtf8Printable(scriptUrl), qUtf8Printable(err));
                        postLinkReady({});
                        return;
                    }
                    const QString postLink = extractPostLinkFromScript(script);
                    if (isValidPostLink(postLink)) {
                        m_cachedPostLinkScript = scriptUrl;
                        m_cachedPostLink = postLink;
                    } else {
                        m_cachedPostLink.clear();
                        m_cachedPostLinkScript.clear();
                        qWarning("Kodik: invalid post_link from %s", qUtf8Printable(scriptUrl));
                    }
                    postLinkReady(postLink);
                },
                4,
                referer);
    }
};

QSharedPointer<KodikSession> sharedSession() {
    static QSharedPointer<KodikSession> session(new KodikSession());
    return session;
}

} // namespace

void KodikClient::loadTranslations(const QString &shikimoriId, TranslationsCallback callback) {
    sharedSession()->loadTranslations(shikimoriId, std::move(callback));
}

void KodikClient::getEpisodeStream(
    const QString &shikimoriId, int episode, const QString &translationId,
    StreamCallback callback) {
    sharedSession()->getEpisodeStream(shikimoriId, episode, translationId, std::move(callback));
}