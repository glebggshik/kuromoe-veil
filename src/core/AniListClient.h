#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// AniList GraphQL — широкие bannerImage для hero-баннера (Shikimori постеры — портретные).
class AniListClient : public QObject {
    Q_OBJECT
public:
    explicit AniListClient(QObject *parent = nullptr) : QObject(parent) {}

    using BannerCallback = std::function<void(QString bannerUrl)>;
    using ItemsCallback = std::function<void(QVariantList items)>;

    struct MediaTitles {
        QString romaji;
        QString english;
        QString native;
    };
    using TitlesCallback = std::function<void(MediaTitles titles)>;

    // Один баннер по malId и/или названию (romaji/english).
    void fetchBanner(int malId, const QString &title, const QString &originalTitle, BannerCallback callback);

    // Английское и японское название по MAL id — для цепочки поиска торрентов.
    void fetchTitles(int malId, TitlesCallback callback);

    // Для каждого тайтла из списка дописывает heroBanner (если AniList нашёл).
    void enrichHeroBanners(const QVariantList &items, ItemsCallback callback);

    // countryOfOrigin (ISO "CN"/"JP"/"KR"/...) по списку malId, одним батч-запросом
    // (idMal_in — до 50 штук за раз, как размер страницы каталога). Shikimori
    // страну происхождения не отдаёт вовсе — только AniList знает её точно, без
    // догадок по названию студии.
    using CountriesCallback = std::function<void(QMap<int, QString> countryByMalId)>;
    void fetchCountriesForMalIds(const QList<int> &malIds, CountriesCallback callback);

private:
    void runQuery(const QString &query, const QVariantMap &variables, BannerCallback callback);
    void searchBanner(const QString &title, const QString &originalTitle, BannerCallback callback);

    QMap<QString, QString> m_bannerCache;
};