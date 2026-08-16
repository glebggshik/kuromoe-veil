#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "AniListClient.h"

// Порт core/catalog.py — клиент graphql-эндпоинта Shikimori (без авторизации,
// даёт постеры/жанры/описание без скрапинга html). Структура запроса
// (animes(...) { ... }) и список полей DEEP_FIELDS взяты 1:1 из Python-версии.
class ShikimoriClient : public QObject {
    Q_OBJECT
public:
    explicit ShikimoriClient(QObject *parent = nullptr) : QObject(parent) {}

    using ListCallback = std::function<void(QVariantList items, QString error)>;
    using ItemCallback = std::function<void(QVariantMap item, QString error)>;
    using CalendarCallback = std::function<void(QVariantList days, QString error)>;

    // search() — полнотекстовый поиск по названию (поле animes(search: ...)).
    void search(
        const QString &title, int limit, int page, bool excludeChinese,
        const QString &kind, const QVariantList &genreIds, const QString &season,
        ListCallback callback);

    // browse() — листинг по статусу/типу/жанру/сортировке (без полнотекстового поиска).
    void browse(
        const QString &status, const QString &kind, const QVariantList &genreIds,
        const QString &order, int page, int limit, bool excludeChinese, const QString &season,
        ListCallback callback);

    // Один тайтл по id — для hero-баннера ("продолжить просмотр") и деталей.
    void getDetails(const QString &shikimoriId, ItemCallback callback);

    // Несколько тайтлов по id — для закладок и батч-обогащения карточек.
    void getByIds(const QStringList &shikimoriIds, ListCallback callback);

    // Связанные тайтлы (сиквелы/приквелы/адаптации) — двухшаговый запрос:
    // related{} в схеме отдаёт только id+тип связи, без постера/счёта, поэтому
    // постеры дотягиваются вторым батч-запросом по этим id (как в catalog.py).
    void getRelated(const QString &shikimoriId, ListCallback callback);

    // Расписание выхода серий онгоингов — REST /api/calendar + graphql-постеры/студии.
    void getCalendar(bool excludeChinese, CalendarCallback callback);

    // {id: name} — полный список жанров Shikimori, для панели фильтров.
    static QVariantList allGenres();

    static bool isChinese(const QVariantMap &item);

    // Qt на Windows без webp-плагина — для QML Image берём jpeg/png, не mainUrl.webp.
    static QString bestPosterUrl(const QString &mainUrl, const QString &hdUrl);

private:
    enum class QueryFields { Catalog, Deep };
    using RawCallback = std::function<void(QJsonArray items, QString error)>;

    void runAnimesQuery(
        const QVariantMap &args, ListCallback callback,
        QueryFields fields = QueryFields::Catalog, bool allowCache = false);
    void enrichCalendarDays(const QVariantList &days, bool excludeChinese, CalendarCallback callback);
    void runRawQuery(const QVariantMap &args, const QStringList &returnFields, RawCallback callback);
    static QString buildQuery(const QVariantMap &args, const QStringList &returnFields);
    static QVariantMap normalize(const QJsonObject &raw);
    static QString stripBbcode(const QString &text);

    // AniList.countryOfOrigin — точнее эвристики по студии (см. isChinese);
    // студия остаётся запасным вариантом там, где у тайтла нет malId или
    // AniList не смог его найти.
    void filterChinese(const QVariantList &items, bool excludeChinese, ListCallback callback);
    AniListClient m_aniList;
};
