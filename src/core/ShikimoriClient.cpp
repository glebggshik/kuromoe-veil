#include "ShikimoriClient.h"

#include <memory>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QHash>
#include <QQueue>
#include <QRegularExpression>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

#include "NetworkManager.h"

namespace {

// shikimori.one теперь 301-редиректит сюда (домен сменился) — обращаемся
// сразу на актуальный, благо Qt по умолчанию не следует за редиректами на
// POST-запросах (вместе с 301 рвалось соединение — "Connection closed").
const QString kGraphqlUrl = "https://shikimori.io/api/graphql";
const QString kCalendarUrl = "https://shikimori.io/api/calendar";
const QString kSiteOrigin = "https://shikimori.io";

using GraphqlCallback = std::function<void(QJsonArray animes, QString error)>;

// Глобальная очередь GraphQL: при старте HomeView + BrowseView шлют десятки
// запросов параллельно (hero, popular, calendar enrich) → Shikimori отвечает 429
// или рвёт TLS ("Connection closed") на Windows.
constexpr int kGraphqlSpacingMs = 480;
constexpr int kGraphqlMaxRetries = 5;

QString rateLimitMessage() {
    return QStringLiteral(
        "Shikimori временно ограничил запросы (слишком часто). "
        "Подождите несколько секунд и обновите список.");
}

bool isRateLimited(QNetworkReply *reply) {
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429;
}

// Qt на Win иногда рвёт HTTPS POST к shikimori.io с RemoteHostClosedError
// ("Connection closed") — это транзиент, надо ретраить, иначе Home остаётся [0].
bool isTransientNetworkError(QNetworkReply::NetworkError err) {
    switch (err) {
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

int retryDelayMs(QNetworkReply *reply, int attempt) {
    const QByteArray retryAfter = reply->rawHeader("Retry-After");
    bool ok = false;
    const int seconds = retryAfter.toInt(&ok);
    if (ok && seconds > 0)
        return seconds * 1000;
    // Connection closed: чуть длиннее пауза, чем 429
    if (isTransientNetworkError(reply->error()))
        return qMin(10000, 800 + 900 * (1 << attempt));
    return qMin(8000, 1500 * (1 << attempt));
}

class GraphqlQueue : public QObject {
public:
    static GraphqlQueue &instance() {
        static GraphqlQueue queue;
        return queue;
    }

    GraphqlQueue() {
        if (QCoreApplication::instance())
            setParent(QCoreApplication::instance());
    }

    void enqueue(const QJsonObject &body, int attempt, GraphqlCallback callback) {
        m_pending.enqueue({body, attempt, std::move(callback)});
        pump();
    }

private:
    struct Job {
        QJsonObject body;
        int attempt = 0;
        GraphqlCallback callback;
    };

    QQueue<Job> m_pending;
    bool m_busy = false;

    void pump() {
        if (m_busy || m_pending.isEmpty())
            return;
        m_busy = true;
        const Job job = m_pending.dequeue();
        dispatch(job);
    }

    void scheduleNext(int delayMs) {
        m_busy = false;
        QTimer::singleShot(delayMs, this, [this]() { pump(); });
    }

    void dispatch(const Job &job) {
        QNetworkRequest req{QUrl(kGraphqlUrl)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("Referer", "https://shikimori.io/");
        req.setRawHeader("Origin", "https://shikimori.io");
        req.setRawHeader(
            "User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");
        // Явно HTTP/1.1 + таймаут — иначе на Win «Connection closed» на первом POST.
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        req.setTransferTimeout(25000);

        // Shikimori не геоблокируется — идём напрямую, без Kodik-прокси.
        QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(job.body).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
            const QByteArray data = reply->readAll();
            const QNetworkReply::NetworkError netErr = reply->error();
            const bool rateLimited = isRateLimited(reply);
            const bool transient = isTransientNetworkError(netErr);
            const int retryDelay = retryDelayMs(reply, job.attempt);
            const QString errorString = reply->errorString();
            reply->deleteLater();

            if ((rateLimited || transient) && job.attempt < kGraphqlMaxRetries) {
                Job retry = job;
                ++retry.attempt;
                m_pending.prepend(retry);
                qWarning("Shikimori GraphQL retry %d/%d after %s (%d ms)",
                         retry.attempt, kGraphqlMaxRetries,
                         qUtf8Printable(errorString), retryDelay);
                scheduleNext(retryDelay);
                return;
            }

            if (netErr != QNetworkReply::NoError) {
                job.callback({}, rateLimited ? rateLimitMessage() : errorString);
                scheduleNext(kGraphqlSpacingMs);
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject root = doc.object();
            if (root.contains("errors")) {
                const QJsonArray errors = root.value("errors").toArray();
                const QString msg = errors.isEmpty()
                                        ? QStringLiteral("Shikimori API error")
                                        : errors[0].toObject().value("message").toString();
                job.callback({}, msg);
                scheduleNext(kGraphqlSpacingMs);
                return;
            }

            job.callback(root.value("data").toObject().value("animes").toArray(), QString());
            scheduleNext(kGraphqlSpacingMs);
        });
    }
};

const QStringList kMonthGenitive = {
    "", "января", "февраля", "марта", "апреля", "мая", "июня",
    "июля", "августа", "сентября", "октября", "ноября", "декабря",
};

const QStringList kWeekdayNames = {
    "", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье",
};

QString calendarDayLabel(const QDate &date, const QDate &today) {
    if (date == today)
        return QStringLiteral("Сегодня");
    if (date == today.addDays(1))
        return QStringLiteral("Завтра");
    const int dow = date.dayOfWeek();
    const int month = date.month();
    if (dow >= 1 && dow <= 7 && month >= 1 && month <= 12)
        return kWeekdayNames[dow] + ", " + QString::number(date.day()) + " " + kMonthGenitive[month];
    return date.toString(QStringLiteral("dd.MM.yyyy"));
}

QString calendarTimeLabel(const QDateTime &dt) {
    if (!dt.isValid())
        return {};
    return dt.toString(QStringLiteral("HH:mm"));
}

bool isMissingShikimoriImage(const QString &path) {
    return path.isEmpty() || path.contains(QStringLiteral("/missing_"));
}

QVariantMap normalizeCalendarEntry(const QJsonObject &entry) {
    const QJsonObject anime = entry.value("anime").toObject();
    const QJsonObject image = anime.value("image").toObject();

    QString title = anime.value("russian").toString();
    if (title.isEmpty())
        title = anime.value("name").toString();
    if (title.isEmpty())
        title = "?";

    const QString preview = image.value("preview").toString();
    const QString original = image.value("original").toString();
    const QString airedOn = anime.value("aired_on").toString();
    int year = 0;
    if (!airedOn.isEmpty()) {
        const QDate airedDate = QDate::fromString(airedOn, QStringLiteral("yyyy-MM-dd"));
        if (airedDate.isValid())
            year = airedDate.year();
    }

    const QString nextEpisodeAt = entry.value("next_episode_at").toString();
    QDateTime airTime = QDateTime::fromString(nextEpisodeAt, Qt::ISODateWithMs);
    if (!airTime.isValid())
        airTime = QDateTime::fromString(nextEpisodeAt, Qt::ISODate);

    QVariantMap out;
    out["id"] = anime.value("id").toVariant().toString();
    out["title"] = title;
    out["originalTitle"] = anime.value("name").toString().trimmed();
    const QString previewUrl =
        isMissingShikimoriImage(preview) ? QString() : kSiteOrigin + preview;
    const QString originalUrl =
        isMissingShikimoriImage(original) ? QString() : kSiteOrigin + original;
    out["posterHd"] = originalUrl.isEmpty() ? previewUrl : originalUrl;
    out["poster"] = ShikimoriClient::bestPosterUrl(previewUrl, originalUrl);
    out["score"] = anime.value("score").toString().toDouble();
    out["kind"] = anime.value("kind").toString();
    out["episodes"] = anime.value("episodes").toInt();
    out["episodesAired"] = anime.value("episodes_aired").toInt();
    out["year"] = year;
    out["status"] = anime.value("status").toString();
    out["nextEpisode"] = entry.value("next_episode").toInt();
    out["nextEpisodeAt"] = nextEpisodeAt;
    out["airTime"] = calendarTimeLabel(airTime);
    out["duration"] = entry.value("duration").toInt();
    out["genreTags"] = QVariantList();
    out["studios"] = QVariantList();
    return out;
}

// Каталог/карточки — без description (экономия трафика и парсинга).
const QStringList kCatalogFields = {
    "id", "name", "russian", "malId", "kind", "status", "score", "episodes", "episodesAired",
    "airedOn { year month day }",
    "poster { previewUrl preview2xUrl mainUrl main2xUrl mainAltUrl originalUrl }",
    "genres { id name russian kind }",
    "studios { name }",
};

const QStringList kDeepFields = {
    "id", "name", "russian", "malId", "kind", "status", "score",
    "episodes", "episodesAired", "duration", "rating",
    "airedOn { year month day }",
    "poster { previewUrl preview2xUrl mainUrl main2xUrl mainAltUrl originalUrl }",
    "genres { id name russian kind }",
    "studios { name }",
    "description",
};

constexpr int kListCacheTtlSec = 8 * 60;
constexpr int kListCacheMaxEntries = 48;

class ListQueryCache {
public:
    static ListQueryCache &instance() {
        static ListQueryCache cache;
        return cache;
    }

    bool get(const QString &key, QVariantList *out) const {
        const auto it = m_entries.constFind(key);
        if (it == m_entries.constEnd())
            return false;
        if (it->expiresAt < QDateTime::currentDateTimeUtc()) {
            m_entries.remove(key);
            return false;
        }
        *out = it->items;
        return true;
    }

    void put(const QString &key, const QVariantList &items) {
        while (m_entries.size() >= kListCacheMaxEntries)
            m_entries.erase(m_entries.begin());
        Entry entry;
        entry.items = items;
        entry.expiresAt = QDateTime::currentDateTimeUtc().addSecs(kListCacheTtlSec);
        m_entries[key] = entry;
    }

private:
    struct Entry {
        QVariantList items;
        QDateTime expiresAt;
    };
    mutable QHash<QString, Entry> m_entries;
};

QString cacheKeyForArgs(const QVariantMap &args, bool catalogFields) {
    QStringList parts;
    parts << (catalogFields ? QStringLiteral("catalog_v3") : QStringLiteral("deep"));
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        parts << it.key() + QLatin1Char('=') + it.value().toString();
    parts.sort();
    return parts.join(QLatin1Char('|'));
}

// Жанры Shikimori — копия GENRES из core/catalog.py (id -> русское название).
const QList<QPair<int, QString>> kGenres = {
    {1, "Экшен"}, {2, "Приключения"}, {3, "Гонки"}, {4, "Комедия"}, {5, "Авангард"},
    {6, "Мифология"}, {7, "Тайна"}, {8, "Драма"}, {9, "Этти"}, {10, "Фэнтези"}, {12, "Хентай"},
    {11, "Стратегические игры"}, {13, "Исторический"}, {14, "Ужасы"}, {15, "Детское"},
    {17, "Боевые искусства"}, {18, "Меха"}, {19, "Музыка"}, {20, "Пародия"},
    {21, "Самураи"}, {22, "Романтика"}, {23, "Школа"}, {24, "Фантастика"},
    {25, "Сёдзё"}, {27, "Сёнен"}, {29, "Космос"}, {30, "Спорт"}, {31, "Супер сила"},
    {32, "Вампиры"}, {35, "Гарем"}, {36, "Повседневность"}, {37, "Сверхъестественное"},
    {38, "Военное"}, {39, "Детектив"}, {40, "Психологическое"}, {42, "Сэйнэн"},
    {43, "Дзёсей"}, {102, "Командный спорт"}, {103, "Видеоигры"},
    {104, "Взрослые персонажи"}, {105, "Жестокость"}, {106, "Реинкарнация"},
    {107, "Любовный многоугольник"}, {108, "Изобразительное искусство"},
    {111, "Путешествие во времени"}, {112, "Гэг-юмор"}, {114, "Удостоено наград"},
    {117, "Триллер"}, {118, "Спортивные единоборства"}, {119, "CGDCT"},
    {124, "Махо-сёдзё"}, {125, "Реверс-гарем"}, {130, "Исэкай"}, {131, "Хулиганы"},
    {134, "Забота о детях"}, {135, "Магическая смена пола"}, {136, "Шоу-бизнес"},
    {137, "Культура отаку"}, {138, "Организованная преступность"}, {139, "Работа"},
    {140, "Иясикэй"}, {141, "Выживание"}, {142, "Исполнительское искусство"},
    {143, "Антропоморфизм"}, {144, "Кроссдрессинг"}, {145, "Идолы (Жен.)"},
    {146, "Игра с высокими ставками"}, {147, "Медицина"}, {148, "Питомцы"},
    {149, "Образовательное"}, {150, "Идолы (Муж.)"}, {151, "Романтический подтекст"},
    {543, "Гурман"},
};

// Эвристика "без китайской анимации" — Shikimori не отдаёт страну происхождения
// напрямую в публичном graphql, поэтому ориентируемся на известные китайские
// студии (см. оригинальный список в core/catalog.py).
const QStringList kChineseStudioMarkers = {
    "shenman", "sparkly key", "cloud art", "ruo hong", "build dream",
    "suoyi", "2:10 animation", "motion magic", "haoliners", "colored pencil",
    "wan wan", "original force", "tencent", "bilibili", "foch",
    "samsara", "big firebird", "mili pictures", "enlight",
    "qing wa", "sunny eye", "yhkt", "wawayu", "noxstar", "alpha group",
    "huanlong", "h.r.melody", "yuanlin", "shanghai motion", "haowei",
    "imagine fanstar", "actoz", "wuhan", "yiqi", "qixingrui", "iyou animation",
    "b.c may", "b.cmay", "bcmay", "light chaser", "nice boat animation",
    "shanghai animation film studio", "vasoon", "puzzle animation",
    "ask animation", "seven stone", "thundray", "alpha animation",
    "creative power entertaining", "l2 studio", "l².studio", "fenz",
    "red dog culture", "garden culture", "delight animation", "qianqi animation",
};

QString genreCsv(const QVariantList &genreIds) {
    QStringList parts;
    for (const QVariant &v : genreIds)
        parts << QString::number(v.toInt());
    return parts.join(",");
}

constexpr int kHentaiGenreId = 12;

bool includesAdultGenre(const QVariantList &genreIds) {
    for (const QVariant &v : genreIds) {
        if (v.toInt() == kHentaiGenreId)
            return true;
    }
    return false;
}

}

QString ShikimoriClient::buildQuery(const QVariantMap &args, const QStringList &returnFields) {
    QStringList parts;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const QString &key = it.key();
        const QVariant &value = it.value();
        if (key == "order") {
            // enum-литерал в graphql-схеме Shikimori — без кавычек
            parts << QString("%1: %2").arg(key, value.toString());
        } else if (value.typeId() == QMetaType::Bool) {
            parts << QString("%1: %2").arg(key, value.toBool() ? "true" : "false");
        } else if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::UInt
                   || value.typeId() == QMetaType::Long || value.typeId() == QMetaType::LongLong
                   || value.typeId() == QMetaType::ULong || value.typeId() == QMetaType::ULongLong
                   || value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float) {
            parts << QString("%1: %2").arg(key, value.toString());
        } else {
            parts << QString("%1: \"%2\"").arg(key, value.toString());
        }
    }
    QString returnBlock = "{\n" + returnFields.join("\n") + "\n}";
    return "{\nanimes(" + parts.join(", ") + ")\n" + returnBlock + "\n}";
}

QString ShikimoriClient::bestPosterUrl(const QString &mainUrl, const QString &hdUrl) {
    auto usable = [](const QString &url) {
        return !url.isEmpty()
            && !url.contains(QStringLiteral("/missing_"))
            && !url.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive);
    };
    if (usable(hdUrl))
        return hdUrl;
    if (usable(mainUrl))
        return mainUrl;
    return hdUrl.isEmpty() ? mainUrl : hdUrl;
}

QString bestPosterFromGraphql(const QJsonObject &poster) {
    auto usable = [](const QString &url) {
        return !url.isEmpty()
            && !url.contains(QStringLiteral("/missing_"))
            && !url.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive);
    };
    const QStringList candidates = {
        poster.value(QStringLiteral("previewUrl")).toString(),
        poster.value(QStringLiteral("preview2xUrl")).toString(),
        poster.value(QStringLiteral("main2xUrl")).toString(),
        poster.value(QStringLiteral("mainAltUrl")).toString(),
        poster.value(QStringLiteral("originalUrl")).toString(),
        poster.value(QStringLiteral("mainUrl")).toString(),
    };
    for (const QString &url : candidates) {
        if (usable(url))
            return url;
    }
    return {};
}

QString ShikimoriClient::stripBbcode(const QString &text) {
    if (text.isEmpty())
        return text;
    QString out = text;

    // 1. Вики-ссылки [[Слово]] — разворачиваем в само слово (это часть
    //    предложения: "[[Для]] Кэйты пришла пора..." → "Для Кэйты...").
    //    Вариант с якорем [[страница|текст]] — берём текст после '|'.
    static const QRegularExpression wikiAliasRe(R"(\[\[[^\[\]\|]*\|([^\[\]]*)\]\])");
    out.replace(wikiAliasRe, QStringLiteral("\\1"));
    static const QRegularExpression wikiRe(R"(\[\[([^\[\]]*)\]\])");
    out.replace(wikiRe, QStringLiteral("\\1"));

    // 2. BBCode-теги [tag], [tag=...], [/tag]. UseUnicodeProperties — иначе
    //    \w матчит только ASCII и кириллические теги ([спойлер]) остаются.
    static const QRegularExpression tagRe(R"(\[(\w+)(?:=[^\]]*)?\]|\[/\w+\])",
                                          QRegularExpression::UseUnicodePropertiesOption);
    out.remove(tagRe);

    // 3. Аннотации с CJK-иероглифами/каной: "Кэйты [住之江 圭太]" — японское
    //    написание имени в скобках, для русского описания это мусор. Убираем
    //    вместе с пробелом перед скобкой.
    static const QRegularExpression cjkAnnotationRe(
        R"(\s*\[[^\[\]]*[\x{3040}-\x{30FF}\x{3400}-\x{4DBF}\x{4E00}-\x{9FFF}\x{F900}-\x{FAFF}\x{FF66}-\x{FF9D}][^\[\]]*\])");
    out.remove(cjkAnnotationRe);

    // 4. Следы разметки: схлопываем множественные пробелы (не трогая \n).
    static const QRegularExpression multiSpaceRe(QStringLiteral("[ \\t]{2,}"));
    out.replace(multiSpaceRe, QStringLiteral(" "));
    // Пробел перед знаком препинания, оставшийся после удаления вставки.
    static const QRegularExpression spaceBeforePunctRe(QStringLiteral(" ([,.;:!?])"));
    out.replace(spaceBeforePunctRe, QStringLiteral("\\1"));

    return out.trimmed();
}

QVariantMap ShikimoriClient::normalize(const QJsonObject &raw) {
    QJsonObject poster = raw.value("poster").toObject();
    QJsonObject airedOn = raw.value("airedOn").toObject();

    QVariantList genreTags;
    for (const QJsonValue &gv : raw.value("genres").toArray()) {
        QJsonObject g = gv.toObject();
        if (g.value("kind").toString() == "genre" && g.contains("id")) {
            QVariantMap tag;
            tag["id"] = g.value("id").toInt();
            QString name = g.value("russian").toString();
            if (name.isEmpty())
                name = g.value("name").toString();
            tag["name"] = name;
            genreTags << tag;
        }
    }

    QVariantList studios;
    for (const QJsonValue &sv : raw.value("studios").toArray()) {
        QString name = sv.toObject().value("name").toString();
        if (!name.isEmpty())
            studios << name;
    }

    QString title = raw.value("russian").toString();
    if (title.isEmpty())
        title = raw.value("name").toString();
    if (title.isEmpty())
        title = "?";

    QString originalTitle = raw.value("name").toString().trimmed();

    const QString posterPick = bestPosterFromGraphql(poster);
    QString posterHd = poster.value("originalUrl").toString();
    if (posterHd.isEmpty())
        posterHd = poster.value("main2xUrl").toString();
    if (posterHd.isEmpty())
        posterHd = poster.value("preview2xUrl").toString();
    if (posterHd.isEmpty())
        posterHd = poster.value("mainUrl").toString();

    QVariantMap out;
    out["id"] = raw.value("id").toVariant().toString();
    // Shikimori отдаёт malId как GraphQL ID, т.е. JSON-СТРОКОЙ ("malId":"1"),
    // а не числом — QJsonValue::toInt() конвертирует только числовой тип и
    // молча возвращает 0 для строкового, так что malId был 0 ВСЕГДА (для
    // любого тайтла). Из-за этого AniList-подстройки по malId (баннер точнее,
    // fetchTitles для цепочки поиска торрентов, фильтр китайской анимации)
    // всегда попадали в фолбэк по названию/студии вместо точного совпадения.
    out["malId"] = raw.value("malId").toString().toInt();
    out["title"] = title;
    out["originalTitle"] = originalTitle;
    out["posterHd"] = posterHd.isEmpty() ? posterPick : posterHd;
    out["poster"] = posterPick.isEmpty() ? bestPosterUrl(poster.value("mainUrl").toString(), posterHd) : posterPick;
    out["score"] = raw.value("score").toVariant();
    out["kind"] = raw.value("kind").toString();
    out["status"] = raw.value("status").toString();
    out["episodes"] = raw.value("episodes").toInt();
    out["episodesAired"] = raw.value("episodesAired").toInt();
    out["year"] = airedOn.value("year").toInt();
    out["genreTags"] = genreTags;
    out["description"] = stripBbcode(raw.value("description").toString());
    out["studios"] = studios;
    return out;
}

bool ShikimoriClient::isChinese(const QVariantMap &item) {
    QStringList studios;
    for (const QVariant &v : item.value("studios").toList())
        studios << v.toString().toLower();
    QString joined = studios.join(" ");
    for (const QString &marker : kChineseStudioMarkers) {
        if (joined.contains(marker))
            return true;
    }
    return false;
}

QVariantList ShikimoriClient::allGenres() {
    QVariantList out;
    for (const auto &pair : kGenres) {
        QVariantMap m;
        m["id"] = pair.first;
        m["name"] = pair.second;
        out << m;
    }
    return out;
}

void ShikimoriClient::runRawQuery(const QVariantMap &args, const QStringList &returnFields, RawCallback callback) {
    QString query = buildQuery(args, returnFields);

    QJsonObject body;
    body["operationName"] = QJsonValue::Null;
    body["variables"] = QJsonObject();
    body["query"] = query;

    GraphqlQueue::instance().enqueue(body, 0, std::move(callback));
}

void ShikimoriClient::runAnimesQuery(
    const QVariantMap &args, ListCallback callback, QueryFields fields, bool allowCache) {
    const bool catalog = fields == QueryFields::Catalog;
    const QStringList &returnFields = catalog ? kCatalogFields : kDeepFields;

    if (allowCache) {
        const QString cacheKey = cacheKeyForArgs(args, catalog);
        QVariantList cached;
        if (ListQueryCache::instance().get(cacheKey, &cached)) {
            callback(cached, QString());
            return;
        }
        runRawQuery(args, returnFields, [callback, cacheKey, allowCache](QJsonArray animes, QString error) {
            if (!error.isEmpty()) {
                callback({}, error);
                return;
            }
            QVariantList items;
            for (const QJsonValue &v : animes)
                items << normalize(v.toObject());
            if (allowCache)
                ListQueryCache::instance().put(cacheKey, items);
            callback(items, QString());
        });
        return;
    }

    runRawQuery(args, returnFields, [callback](QJsonArray animes, QString error) {
        if (!error.isEmpty()) {
            callback({}, error);
            return;
        }
        QVariantList items;
        for (const QJsonValue &v : animes)
            items << normalize(v.toObject());
        callback(items, QString());
    });
}

void ShikimoriClient::filterChinese(
    const QVariantList &items, bool excludeChinese, ListCallback callback) {
    if (!excludeChinese || items.isEmpty()) {
        callback(items, QString());
        return;
    }

    QList<int> malIds;
    for (const QVariant &v : items) {
        const int malId = v.toMap().value(QStringLiteral("malId")).toInt();
        if (malId > 0)
            malIds << malId;
    }
    if (malIds.isEmpty()) {
        // Без malId AniList не поможет — старая эвристика по студии как
        // единственный доступный вариант.
        QVariantList filtered;
        for (const QVariant &v : items) {
            if (!isChinese(v.toMap()))
                filtered << v;
        }
        callback(filtered, QString());
        return;
    }

    m_aniList.fetchCountriesForMalIds(malIds, [items, callback](QMap<int, QString> countries) {
        QVariantList filtered;
        for (const QVariant &v : items) {
            const QVariantMap m = v.toMap();
            const int malId = m.value(QStringLiteral("malId")).toInt();
            const QString country = countries.value(malId);
            // Есть точный ответ от AniList — доверяем ему целиком (даже если он
            // расходится со старой эвристикой по студии). Нет ответа (malId=0,
            // AniList не нашёл тайтл, сетевая ошибка) — откат на эвристику по
            // студии, чтобы не терять фильтрацию совсем.
            const bool chinese = !country.isEmpty() ? (country == QStringLiteral("CN")) : isChinese(m);
            if (!chinese)
                filtered << v;
        }
        callback(filtered, QString());
    });
}

void ShikimoriClient::search(
    const QString &title, int limit, int page, bool excludeChinese,
    const QString &kind, const QVariantList &genreIds, const QString &season,
    ListCallback callback) {
    QVariantMap args;
    args["search"] = title;
    args["limit"] = limit;
    args["page"] = page;
    if (!kind.isEmpty())
        args["kind"] = kind;
    if (!genreIds.isEmpty())
        args["genre"] = genreCsv(genreIds);
    if (!season.isEmpty())
        args["season"] = season;
    args["censored"] = !includesAdultGenre(genreIds);

    runAnimesQuery(args, [this, excludeChinese, callback](QVariantList items, QString error) {
        if (!error.isEmpty()) {
            callback(items, error);
            return;
        }
        filterChinese(items, excludeChinese, callback);
    }, QueryFields::Catalog, true);
}

void ShikimoriClient::browse(
    const QString &status, const QString &kind, const QVariantList &genreIds,
    const QString &order, int page, int limit, bool excludeChinese, const QString &season,
    ListCallback callback) {
    QVariantMap args;
    args["limit"] = limit;
    args["page"] = page;
    args["order"] = order;
    args["censored"] = !includesAdultGenre(genreIds);
    if (!status.isEmpty())
        args["status"] = status;
    if (!kind.isEmpty())
        args["kind"] = kind;
    if (!genreIds.isEmpty())
        args["genre"] = genreCsv(genreIds);
    if (!season.isEmpty())
        args["season"] = season;

    runAnimesQuery(args, [this, excludeChinese, callback](QVariantList items, QString error) {
        if (!error.isEmpty()) {
            callback(items, error);
            return;
        }
        filterChinese(items, excludeChinese, callback);
    }, QueryFields::Catalog, true);
}

void ShikimoriClient::getDetails(const QString &shikimoriId, ItemCallback callback) {
    QVariantMap args;
    args["ids"] = shikimoriId;
    args["limit"] = 1;
    runAnimesQuery(args, [callback](QVariantList items, QString error) {
        if (!error.isEmpty()) {
            callback({}, error);
            return;
        }
        callback(items.isEmpty() ? QVariantMap() : items.first().toMap(), QString());
    }, QueryFields::Deep, false);
}

void ShikimoriClient::getByIds(const QStringList &shikimoriIds, ListCallback callback) {
    QStringList ids;
    for (const QString &id : shikimoriIds) {
        if (!id.isEmpty())
            ids << id;
    }
    if (ids.isEmpty()) {
        callback({}, QString());
        return;
    }

    QVariantMap args;
    args["ids"] = ids.join(",");
    args["limit"] = static_cast<int>(qMin(ids.size(), 50));
    runAnimesQuery(args, callback, QueryFields::Catalog, false);
}

void ShikimoriClient::enrichCalendarDays(
    const QVariantList &days, bool excludeChinese, CalendarCallback callback) {
    QStringList allIds;
    for (const QVariant &dayV : days) {
        for (const QVariant &itemV : dayV.toMap().value("items").toList()) {
            const QString id = itemV.toMap().value("id").toString();
            if (!id.isEmpty() && !allIds.contains(id))
                allIds << id;
        }
    }

    if (allIds.isEmpty()) {
        callback(days, QString());
        return;
    }

    struct EnrichState {
        QMap<QString, QVariantMap> byId;
        QString error;
    };
    auto state = std::make_shared<EnrichState>();

    const int chunkSize = 50;

    auto finish = [this, state, days, excludeChinese, callback]() {
        if (!state->error.isEmpty()) {
            // Постеры — бонус; при 429/ошибке GraphQL оставляем REST-расписание.
            callback(days, QString());
            return;
        }

        // Раньше тут фильтровали через голый isChinese(item) — эвристику
        // по студии. Календарные тайтлы из REST /api/calendar приходят без
        // malId, а enrichment (ниже) копировал только poster/studios, не
        // malId — так что AniList-путь в isChinese был в принципе
        // недостижим, оставалась только студия, а если студии в энричменте
        // не оказалось (не успело подтянуться/не нашлось по id) — тайтл
        // просто не фильтровался никак. Прогоняем весь список через тот же
        // filterChinese(), что и browse()/search() (AniList countryOfOrigin,
        // с эвристикой по студии как запасной вариант) — для этого сначала
        // копируем malId из enrichment и расплющиваем все дни в один список
        // с меткой дня, чтобы потом собрать обратно.
        QVariantList flatItems;
        for (int d = 0; d < days.size(); ++d) {
            const QVariantMap day = days.at(d).toMap();
            for (const QVariant &itemV : day.value("items").toList()) {
                QVariantMap item = itemV.toMap();
                const QString id = item.value("id").toString();
                if (state->byId.contains(id)) {
                    const QVariantMap enriched = state->byId.value(id);
                    const QString poster = enriched.value("poster").toString();
                    const QString posterHd = enriched.value("posterHd").toString();
                    if (!poster.isEmpty())
                        item["poster"] = poster;
                    if (!posterHd.isEmpty())
                        item["posterHd"] = posterHd;
                    item["studios"] = enriched.value("studios");
                    item["genreTags"] = enriched.value("genreTags");
                    item["malId"] = enriched.value("malId");
                }
                item["_dayIndex"] = d;
                flatItems << item;
            }
        }

        filterChinese(flatItems, excludeChinese, [days, callback](QVariantList filtered, QString) {
            QMap<int, QVariantList> byDay;
            for (const QVariant &v : filtered) {
                QVariantMap item = v.toMap();
                const int d = item.value("_dayIndex").toInt();
                item.remove("_dayIndex");
                byDay[d] << item;
            }
            QVariantList outDays;
            for (int d = 0; d < days.size(); ++d) {
                if (!byDay.contains(d))
                    continue;
                QVariantMap day = days.at(d).toMap();
                day["items"] = byDay.value(d);
                outDays << day;
            }
            callback(outDays, QString());
        });
    };

    // Нельзя захватывать std::function по значению в момент присваивания —
    // поймается пустая копия, рекурсивный вызов → std::bad_function_call (вылет
    // календаря после первого GraphQL-чанка). Держим функцию в shared_ptr.
    auto fetchChunk = std::make_shared<std::function<void(int)>>();
    *fetchChunk = [this, state, allIds, chunkSize, finish, fetchChunk](int offset) {
        if (!state->error.isEmpty()) {
            finish();
            return;
        }
        if (offset >= allIds.size()) {
            finish();
            return;
        }
        const QStringList chunk = allIds.mid(offset, chunkSize);
        QVariantMap args;
        args["ids"] = chunk.join(",");
        args["limit"] = chunk.size();
        runAnimesQuery(args, [state, offset, chunkSize, finish, fetchChunk](QVariantList items, QString error) {
            if (!error.isEmpty()) {
                state->error = error;
                finish();
                return;
            }
            for (const QVariant &v : items)
                state->byId.insert(v.toMap().value("id").toString(), v.toMap());
            (*fetchChunk)(offset + chunkSize);
        });
    };
    (*fetchChunk)(0);
}

void ShikimoriClient::getCalendar(bool excludeChinese, CalendarCallback callback) {
    QNetworkRequest req{QUrl(kCalendarUrl)};
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");

    QNetworkReply *reply = NetworkManager::instance()->getLocal(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, excludeChinese, callback]() {
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        const bool rateLimited = isRateLimited(reply);
        const QString errorString = reply->errorString();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError) {
            callback({}, rateLimited ? rateLimitMessage() : errorString);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            callback({}, QStringLiteral("Некорректный ответ календаря Shikimori"));
            return;
        }

        const QTimeZone msk = QTimeZone(QByteArray("Europe/Moscow"));
        const QDate today = QDateTime::currentDateTime().toTimeZone(msk).date();

        QMap<QString, QVariantList> itemsByDate;
        QStringList dateOrder;

        for (const QJsonValue &v : doc.array()) {
            const QJsonObject entry = v.toObject();
            const QString nextEpisodeAt = entry.value("next_episode_at").toString();
            QDateTime airTime = QDateTime::fromString(nextEpisodeAt, Qt::ISODateWithMs);
            if (!airTime.isValid())
                airTime = QDateTime::fromString(nextEpisodeAt, Qt::ISODate);
            if (!airTime.isValid())
                continue;

            const QDate date = airTime.toTimeZone(msk).date();
            const QString dateKey = date.toString(QStringLiteral("yyyy-MM-dd"));
            if (!itemsByDate.contains(dateKey))
                dateOrder << dateKey;
            itemsByDate[dateKey] << normalizeCalendarEntry(entry);
        }

        QVariantList days;
        for (const QString &dateKey : dateOrder) {
            const QDate date = QDate::fromString(dateKey, QStringLiteral("yyyy-MM-dd"));
            QVariantMap day;
            day["label"] = calendarDayLabel(date, today);
            day["date"] = dateKey;
            day["items"] = itemsByDate.value(dateKey);
            days << day;
        }
        // Сначала REST (расписание сразу), затем GraphQL-постеры без блокировки UI.
        callback(days, QString());
#ifndef NDEBUG
        // MSVC Debug: сам вызов enrichCalendarDays() (batch GraphQL-запрос
        // getByIds по ~100+ id разом) валит abort() в Debug-рантайме — не
        // только назначение постеров (assignPosters=false тоже падает,
        // проверено). Оставляем расписание без фильтра "скрывать китайские
        // аниме" и без постеров/студий в Debug; в Release enrichCalendarDays
        // отрабатывает нормально и фильтр применяется.
        Q_UNUSED(excludeChinese);
#else
        enrichCalendarDays(days, excludeChinese, callback);
#endif
    });
}

void ShikimoriClient::getRelated(const QString &shikimoriId, ListCallback callback) {
    QVariantMap step1Args;
    step1Args["ids"] = shikimoriId;
    step1Args["limit"] = 1;
    QStringList step1Fields = {"related { id anime { id } relationKind relationText }"};

    runRawQuery(step1Args, step1Fields, [this, callback](QJsonArray rows, QString error) {
        if (!error.isEmpty() || rows.isEmpty()) {
            callback({}, error);
            return;
        }
        QJsonArray related = rows.first().toObject().value("related").toArray();

        // id связанного тайтла -> текст связи ("Сиквел", "Адаптация" и т.п.)
        QMap<QString, QString> relationById;
        QStringList ids;
        for (const QJsonValue &rv : related) {
            QJsonObject r = rv.toObject();
            QJsonObject anime = r.value("anime").toObject();
            if (!anime.contains("id"))
                continue;
            QString id = anime.value("id").toVariant().toString();
            QString relation = r.value("relationText").toString();
            if (relation.isEmpty())
                relation = r.value("relationKind").toString();
            relationById[id] = relation;
            ids << id;
        }
        if (ids.isEmpty()) {
            callback({}, QString());
            return;
        }

        QVariantMap step2Args;
        step2Args["ids"] = ids.join(",");
        step2Args["limit"] = 50;
        runAnimesQuery(step2Args, [relationById, callback](QVariantList items, QString error2) {
            if (!error2.isEmpty()) {
                callback({}, error2);
                return;
            }
            QVariantList out;
            for (QVariant v : items) {
                QVariantMap item = v.toMap();
                item["relation"] = relationById.value(item.value("id").toString());
                out << item;
            }
            callback(out, QString());
        });
    });
}
