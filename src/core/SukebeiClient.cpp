#include "SukebeiClient.h"

#include <algorithm>

#include <QSet>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include "NetworkManager.h"

namespace {

QString decodeHtmlEntities(const QString &text) {
    QString out = text;
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    return out;
}

QString stripTags(const QString &html) {
    static const QRegularExpression tagRe(QStringLiteral("<[^>]+>"));
    return decodeHtmlEntities(html).replace(tagRe, QString()).trimmed();
}

QVariantList parseSearchHtml(const QByteArray &html) {
    const QString body = QString::fromUtf8(html);
    static const QRegularExpression rowRe(
        QStringLiteral(R"re(<tr class="(?:default|success|danger)">(.*?)</tr>)re"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression titleRe(
        QStringLiteral(R"re(<a href="/view/\d+"[^>]*title="([^"]*)"[^>]*>([^<]*)</a>)re"));
    static const QRegularExpression magnetRe(QStringLiteral(R"re(href="(magnet:[^"]+)")re"));
    static const QRegularExpression countersRe(QStringLiteral(
        R"re(<td class="text-center">(\d+)</td>\s*<td class="text-center">(\d+)</td>\s*<td class="text-center">(\d+)</td>)re"));

    QVariantList out;
    QSet<QString> seenMagnets;
    auto it = rowRe.globalMatch(body);
    while (it.hasNext()) {
        const QString row = it.next().captured(1);
        const auto titleMatch = titleRe.match(row);
        const auto magnetMatch = magnetRe.match(row);
        if (!titleMatch.hasMatch() || !magnetMatch.hasMatch())
            continue;

        QString title = titleMatch.captured(1).trimmed();
        if (title.isEmpty())
            title = stripTags(titleMatch.captured(2));
        const QString magnet = decodeHtmlEntities(magnetMatch.captured(1));
        if (title.isEmpty() || magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;

        int seeders = 0;
        const auto counters = countersRe.match(row);
        if (counters.hasMatch())
            seeders = counters.captured(1).toInt();

        QVariantMap item;
        item[QStringLiteral("title")] = title;
        item[QStringLiteral("magnet")] = magnet;
        item[QStringLiteral("size")] = QStringLiteral("?");
        item[QStringLiteral("seeders")] = seeders;
        item[QStringLiteral("tracker")] = QStringLiteral("Sukebei");
        seenMagnets.insert(magnet);
        out << item;
    }

    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("seeders")).toInt()
            > b.toMap().value(QStringLiteral("seeders")).toInt();
    });
    return out;
}

} // namespace

QString SukebeiClient::humanSize(qint64 bytes) {
    if (bytes <= 0)
        return QStringLiteral("?");
    double size = static_cast<double>(bytes);
    const QStringList units = {QStringLiteral("Б"), QStringLiteral("КБ"), QStringLiteral("МБ"),
                               QStringLiteral("ГБ"), QStringLiteral("ТБ")};
    for (int i = 0; i < units.size(); ++i) {
        if (size < 1024.0 || i == units.size() - 1)
            return QString::number(size, 'f', 1) + QLatin1Char(' ') + units[i];
        size /= 1024.0;
    }
    return QString();
}

void SukebeiClient::search(const QString &title, std::function<void(QVariantList, QString, int)> callback) {
    const QString query = title.trimmed();
    if (query.isEmpty()) {
        callback({}, QString(), 0);
        return;
    }

    QUrl url(QStringLiteral("https://sukebei.nyaa.si/"));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("f"), QStringLiteral("0"));
    urlQuery.addQueryItem(QStringLiteral("c"), QStringLiteral("0_0"));
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    urlQuery.addQueryItem(QStringLiteral("s"), QStringLiteral("seeders"));
    urlQuery.addQueryItem(QStringLiteral("o"), QStringLiteral("desc"));
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

    // sukebei — публичный сайт, как JacRed, без прокси.
    QNetworkReply *reply = NetworkManager::instance()->getLocal(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError err = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (err != QNetworkReply::NoError) {
            callback({}, QString(), httpStatus);
            return;
        }
        callback(parseSearchHtml(data), QString(), httpStatus);
    });
}