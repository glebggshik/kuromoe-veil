#include "AniListClient.h"

#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "NetworkManager.h"

namespace {

const QString kGraphqlUrl = QStringLiteral("https://graphql.anilist.co");

QString cacheKey(int malId, const QString &title, const QString &originalTitle) {
    if (malId > 0)
        return QStringLiteral("mal:%1").arg(malId);
    const QString t = originalTitle.trimmed();
    if (!t.isEmpty())
        return QStringLiteral("title:%1").arg(t.toLower());
    return QStringLiteral("title:%1").arg(title.trimmed().toLower());
}

bool usableBannerUrl(const QString &url) {
    return !url.isEmpty() && !url.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive);
}

QString extractBanner(const QJsonObject &media) {
    const QString banner = media.value(QStringLiteral("bannerImage")).toString();
    return usableBannerUrl(banner) ? banner : QString();
}

} // namespace

void AniListClient::runQuery(const QString &query, const QVariantMap &variables, BannerCallback callback) {
    QJsonObject body;
    body[QStringLiteral("query")] = query;
    if (!variables.isEmpty()) {
        QJsonObject vars;
        for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
            vars[it.key()] = QJsonValue::fromVariant(it.value());
        body[QStringLiteral("variables")] = vars;
    }

    QNetworkRequest req{QUrl(kGraphqlUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");

    // AniList не геоблокируется — без прокси (как Shikimori/JacRed).
    QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(body).toJson());
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError) {
            callback(QString());
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(data).object();
        if (root.contains(QStringLiteral("errors"))) {
            callback(QString());
            return;
        }

        const QJsonObject dataObj = root.value(QStringLiteral("data")).toObject();
        QString banner = extractBanner(dataObj.value(QStringLiteral("Media")).toObject());
        if (banner.isEmpty()) {
            const QJsonArray mediaList = dataObj.value(QStringLiteral("Page"))
                .toObject()
                .value(QStringLiteral("media"))
                .toArray();
            if (!mediaList.isEmpty())
                banner = extractBanner(mediaList.first().toObject());
        }
        callback(banner);
    });
}

void AniListClient::searchBanner(
    const QString &title, const QString &originalTitle, BannerCallback callback) {
    const QString search = originalTitle.trimmed().isEmpty() ? title.trimmed() : originalTitle.trimmed();
    if (search.isEmpty()) {
        callback(QString());
        return;
    }

    const QString key = cacheKey(0, title, originalTitle);
    if (m_bannerCache.contains(key)) {
        callback(m_bannerCache.value(key));
        return;
    }

    static const QString query = QStringLiteral(
        "query ($search: String) {"
        "  Page(perPage: 1) {"
        "    media(search: $search, type: ANIME, sort: SEARCH_MATCH) {"
        "      bannerImage"
        "    }"
        "  }"
        "}");

    QVariantMap vars;
    vars[QStringLiteral("search")] = search;
    runQuery(query, vars, [this, key, callback](QString banner) {
        if (!banner.isEmpty())
            m_bannerCache.insert(key, banner);
        callback(banner);
    });
}

void AniListClient::fetchBanner(
    int malId, const QString &title, const QString &originalTitle, BannerCallback callback) {
    const QString key = cacheKey(malId, title, originalTitle);
    if (m_bannerCache.contains(key)) {
        callback(m_bannerCache.value(key));
        return;
    }

    if (malId > 0) {
        static const QString query = QStringLiteral(
            "query ($idMal: Int) {"
            "  Media(idMal: $idMal, type: ANIME) {"
            "    bannerImage"
            "  }"
            "}");
        QVariantMap vars;
        vars[QStringLiteral("idMal")] = malId;
        runQuery(query, vars, [this, malId, title, originalTitle, key, callback](QString banner) {
            if (!banner.isEmpty()) {
                m_bannerCache.insert(key, banner);
                callback(banner);
                return;
            }
            searchBanner(title, originalTitle, callback);
        });
        return;
    }

    searchBanner(title, originalTitle, callback);
}

void AniListClient::fetchTitles(int malId, TitlesCallback callback) {
    if (malId <= 0) {
        callback({});
        return;
    }

    static const QString query = QStringLiteral(
        "query ($idMal: Int) {"
        "  Media(idMal: $idMal, type: ANIME) {"
        "    title { romaji english native }"
        "  }"
        "}");

    QJsonObject body;
    body[QStringLiteral("query")] = query;
    QJsonObject vars;
    vars[QStringLiteral("idMal")] = malId;
    body[QStringLiteral("variables")] = vars;

    QNetworkRequest req{QUrl(kGraphqlUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");

    // AniList не геоблокируется — без прокси (как Shikimori/JacRed).
    QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(body).toJson());
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        MediaTitles titles;
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        reply->deleteLater();

        if (netErr == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            if (!root.contains(QStringLiteral("errors"))) {
                const QJsonObject titleObj = root.value(QStringLiteral("data"))
                    .toObject()
                    .value(QStringLiteral("Media"))
                    .toObject()
                    .value(QStringLiteral("title"))
                    .toObject();
                titles.romaji = titleObj.value(QStringLiteral("romaji")).toString().trimmed();
                titles.english = titleObj.value(QStringLiteral("english")).toString().trimmed();
                titles.native = titleObj.value(QStringLiteral("native")).toString().trimmed();
            }
        }
        callback(titles);
    });
}

void AniListClient::fetchCountriesForMalIds(const QList<int> &malIds, CountriesCallback callback) {
    if (malIds.isEmpty()) {
        callback({});
        return;
    }

    static const QString query = QStringLiteral(
        "query ($ids: [Int]) {"
        "  Page(perPage: 50) {"
        "    media(idMal_in: $ids, type: ANIME) {"
        "      idMal"
        "      countryOfOrigin"
        "    }"
        "  }"
        "}");

    QJsonArray idsArray;
    for (int id : malIds)
        idsArray.append(id);

    QJsonObject body;
    body[QStringLiteral("query")] = query;
    QJsonObject vars;
    vars[QStringLiteral("ids")] = idsArray;
    body[QStringLiteral("variables")] = vars;

    QNetworkRequest req{QUrl(kGraphqlUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");

    // AniList не геоблокируется — без прокси (как Shikimori/JacRed).
    QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(body).toJson());
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        QMap<int, QString> result;
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        reply->deleteLater();

        if (netErr == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            if (!root.contains(QStringLiteral("errors"))) {
                const QJsonArray mediaList = root.value(QStringLiteral("data"))
                    .toObject()
                    .value(QStringLiteral("Page"))
                    .toObject()
                    .value(QStringLiteral("media"))
                    .toArray();
                for (const QJsonValue &mv : mediaList) {
                    const QJsonObject m = mv.toObject();
                    const int idMal = m.value(QStringLiteral("idMal")).toInt();
                    const QString country = m.value(QStringLiteral("countryOfOrigin")).toString();
                    if (idMal > 0 && !country.isEmpty())
                        result.insert(idMal, country);
                }
            }
        }
        callback(result);
    });
}

void AniListClient::enrichHeroBanners(const QVariantList &items, ItemsCallback callback) {
    if (items.isEmpty()) {
        callback(items);
        return;
    }

    struct State {
        QVariantList out;
        int pending = 0;
    };
    auto state = std::make_shared<State>();
    state->out.reserve(items.size());

    for (const QVariant &v : items) {
        QVariantMap item = v.toMap();
        state->out << item;
        ++state->pending;
    }

    for (int i = 0; i < items.size(); ++i) {
        const QVariantMap item = items[i].toMap();
        const int malId = item.value(QStringLiteral("malId")).toInt();
        const QString title = item.value(QStringLiteral("title")).toString();
        const QString originalTitle = item.value(QStringLiteral("originalTitle")).toString();

        fetchBanner(malId, title, originalTitle, [state, i, callback](QString banner) {
            if (!banner.isEmpty()) {
                QVariantMap updated = state->out[i].toMap();
                updated[QStringLiteral("heroBanner")] = banner;
                state->out[i] = updated;
            }
            if (--state->pending == 0)
                callback(state->out);
        });
    }
}