#include "DetailBridge.h"

#include <algorithm>
#include <memory>

#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include "HistoryManager.h"
#include "PosterCache.h"
#include "ShikimoriClient.h"
#include "StatusStore.h"

namespace {

constexpr int kHentaiGenreId = 12;
constexpr int kTorrentMinScoreDefault = 12;
constexpr int kTorrentMinScoreHentai = 3;

// Кэш результатов поиска торрентов по titleId — живёт в статике процесса, то
// есть до закрытия приложения (не персистится на диск). Поиск через JacRed/
// Sukebei медленный (несколько последовательных запросов с паузами против
// рейт-лимита, см. jacredJitterMs) — при повторном заходе на тот же тайтл
// (возврат назад, "Связанное", смена и снова тот же источник) нет смысла
// гонять его заново.
QHash<QString, QVariantList> g_torrentSearchCache;

// jac.red агрессивно рейт-лимитит (429 Too Many Requests) уже на ВТОРОМ
// запросе, если слать их без паузы — а поиск торрентов делает несколько
// последовательных запросов с разными вариантами названия (см. runQueryAt
// в searchTorrents). Без задержки реально находились только результаты
// первого запроса — все "уточняющие" запросы молча падали в 429 и
// возвращали 0, из-за чего для многих тайтлов торренты просто не
// показывались, хотя раздачи на самом деле есть.
int jacredJitterMs(int minMs, int maxMs) {
    if (maxMs <= minMs)
        return minMs;
    return minMs + static_cast<int>(QRandomGenerator::global()->bounded(maxMs - minMs));
}

bool isHentaiItem(const QVariantMap &item) {
    for (const QVariant &v : item.value(QStringLiteral("genreTags")).toList()) {
        if (v.toMap().value(QStringLiteral("id")).toInt() == kHentaiGenreId)
            return true;
    }
    return false;
}

// Порт _matches_year/_matches_kind из qt_bridge/detail_bridge.py — JacRed
// ищет по подстроке без учёта сезона/типа релиза, эти эвристики отсеивают
// явно посторонние раздачи (другой сезон/год, фильм вместо ТВ-сериала и т.п.)
bool matchesYear(const QString &title, int year) {
    if (year <= 0)
        return true;
    static const QRegularExpression re(R"((?:19|20)\d{2})");
    auto it = re.globalMatch(title);
    bool any = false;
    while (it.hasNext()) {
        any = true;
        if (it.next().captured(0).toInt() == year)
            return true;
    }
    return !any;
}

bool looksLikeTvSeriesPack(const QString &title) {
    const QString t = title.toLower();
    static const QRegularExpression rangeRe(R"((?:\[|\()\s*\d+\s*[-–—]\s*\d+)");
    if (rangeRe.match(t).hasMatch())
        return true;
    if (t.contains(QStringLiteral("сезон")) || t.contains(QStringLiteral("season")))
        return true;
    static const QRegularExpression seasonRe(R"(\bs\d{1,2}\b)");
    if (seasonRe.match(t).hasMatch())
        return true;
    static const QRegularExpression episodeRe(R"(из\s*(\d+))");
    const auto m = episodeRe.match(t);
    return m.hasMatch() && m.captured(1).toInt() > 3;
}

bool matchesKind(const QString &title, const QString &kind) {
    if (kind.isEmpty() || title.isEmpty())
        return true;
    const QString t = title.toLower();
    static const QRegularExpression episodeRe(R"(из\s*(\d+))");
    const auto m = episodeRe.match(t);
    const int episodeCount = m.hasMatch() ? m.captured(1).toInt() : 0;
    const bool isMovieRelease = t.contains(QStringLiteral("фильм")) || t.contains(QStringLiteral("movie"))
        || t.contains(QStringLiteral("film"));

    if (kind == QStringLiteral("tv") || kind == QStringLiteral("tv_special")) {
        if (isMovieRelease && episodeCount <= 3)
            return false;
        return true;
    }
    if (kind == QStringLiteral("movie") || kind == QStringLiteral("ova") || kind == QStringLiteral("ona")
        || kind == QStringLiteral("special") || kind == QStringLiteral("music")) {
        if (looksLikeTvSeriesPack(title))
            return false;
        return true;
    }
    return true;
}

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

void appendUniqueQuery(QStringList *queries, QSet<QString> *seen, const QString &query) {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    const QString key = trimmed.toLower();
    if (seen->contains(key))
        return;
    seen->insert(key);
    queries->append(trimmed);
}

QString stripParentheses(const QString &text) {
    QString s = text;
    static const QRegularExpression parenRe(R"(\([^)]*\))");
    s.remove(parenRe);
    static const QRegularExpression spaceRe(R"(\s+)");
    s = s.trimmed();
    s.replace(spaceRe, QStringLiteral(" "));
    return s;
}

QString compactSpaces(const QString &text) {
    return text.trimmed().replace(QRegularExpression(R"(\s+)"), QStringLiteral(" "));
}

QString compactNoSpaces(const QString &text) {
    return compactSpaces(stripParentheses(text)).remove(QLatin1Char(' '));
}

QString extractVersionToken(const QString &text) {
    static const QRegularExpression versionRe(R"(\b(\d+\.\d+)\b)");
    const auto m = versionRe.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString extractFranchiseBase(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QString version = extractVersionToken(trimmed);
    if (!version.isEmpty()) {
        const QString before = compactSpaces(trimmed.left(trimmed.indexOf(version)).trimmed());
        if (!before.isEmpty()) {
            const int sp = before.indexOf(QLatin1Char(' '));
            return sp > 0 ? before.left(sp) : before;
        }
    }
    static const QRegularExpression firstWordRe(
        QStringLiteral(R"(^([\p{L}\p{N}]+))"), QRegularExpression::UseUnicodePropertiesOption);
    const auto m = firstWordRe.match(trimmed);
    if (m.hasMatch()) {
        QString word = m.captured(1);
        int pos = m.capturedEnd(1);
        // Короткое первое слово ("Re" в "Re:Zero", "One" в "One Piece", "Ван"
        // в "Ван-Пис") само по себе не годится как признак франшизы — мало
        // очков в scoreTorrentRelevance и риск ложных совпадений. Приклеиваем
        // следующее слово (через любой разделитель — двоеточие/дефис/пробел).
        // Раньше останавливались, как только слово набирало 4+ символов — но
        // одно обычное слово такой длины ("Семья", "Меч" и т.п.) само по себе
        // слишком общее: "Семья шпиона" получало базу "Семья" и совпадало с
        // посторонним ситкомом "Семья" (реальный кейс — запустился не тот
        // торрент). Берём минимум 2 слова, если они есть в названии, вне
        // зависимости от длины первого.
        int wordsUsed = 1;
        while ((word.length() < 4 || wordsUsed < 2) && pos < trimmed.length()) {
            while (pos < trimmed.length() && !trimmed.at(pos).isLetterOrNumber())
                ++pos;
            if (pos >= trimmed.length())
                break;
            // match(trimmed, offset) не годится: "^" в firstWordRe якорится на
            // начало ВСЕЙ строки, а не на offset — искали подстроку через
            // mid(), чтобы "^" якорился на её начало.
            const auto m2 = firstWordRe.match(trimmed.mid(pos));
            if (!m2.hasMatch())
                break;
            word += m2.captured(1);
            pos += m2.capturedEnd(1);
            ++wordsUsed;
        }
        if (word.length() >= 4)
            return word;
    }
    return {};
}

void appendQueryVariants(QStringList *queries, QSet<QString> *seen, const QString &query) {
    const QString compact = compactSpaces(query);
    if (compact.isEmpty())
        return;
    appendUniqueQuery(queries, seen, compact);
    const QString stripped = stripParentheses(compact);
    if (stripped != compact)
        appendUniqueQuery(queries, seen, stripped);
    const QString nospace = compactNoSpaces(compact);
    if (!nospace.isEmpty() && nospace.length() >= 4)
        appendUniqueQuery(queries, seen, nospace);
}

void appendTitleQueries(QStringList *queries, QSet<QString> *seen, const QString &primary) {
    const QString trimmed = compactSpaces(primary);
    if (trimmed.isEmpty())
        return;

    const QString version = extractVersionToken(trimmed);
    // >= 4, а не > 0 — иначе "Re:Zero...", "K:...' и т.п. (двоеточие внутри
    // самого названия, не разделитель "Заголовок: Подзаголовок") ошибочно
    // режутся на бесполезный 2-символьный запрос ("Re") и обрезанный хвост.
    const int colon = trimmed.indexOf(QLatin1Char(':'));
    const QString colonLeft = colon >= 4 ? compactSpaces(trimmed.left(colon)) : QString();
    const QString colonRight = colon >= 4 ? stripParentheses(trimmed.mid(colon + 1)) : QString();
    const QString base = extractFranchiseBase(trimmed);

    // Самые точные запросы — с версией (1.11 / 2.22) или коротким подзаголовком.
    if (!version.isEmpty()) {
        if (!colonLeft.isEmpty())
            appendQueryVariants(queries, seen, colonLeft);
        appendQueryVariants(queries, seen, version);
        if (!base.isEmpty())
            appendQueryVariants(queries, seen, base + QLatin1Char(' ') + version);
    } else if (!colonRight.isEmpty() && colonRight.length() >= 4) {
        appendQueryVariants(queries, seen, colonRight);
    }

    if (!colonLeft.isEmpty() && colonLeft != trimmed)
        appendQueryVariants(queries, seen, colonLeft);

    if (!version.isEmpty() && !colonRight.isEmpty() && colonRight.length() >= 4) {
        const QString combo = colonLeft.isEmpty()
            ? (base + QLatin1Char(' ') + version + QLatin1Char(' ') + colonRight).trimmed()
            : (colonLeft + QLatin1Char(' ') + colonRight).trimmed();
        appendQueryVariants(queries, seen, combo);
    }

    appendQueryVariants(queries, seen, trimmed);

    // '—' в UTF-8 — многобайтовый символ, поэтому литерал '—' компилятор усекает
    // до 0x94 и разделитель никогда не совпадает с настоящим U+2014. Берём QChar явно.
    for (const QChar sep : {QChar(u'/'), QChar(0x2014)}) {
        const int idx = trimmed.indexOf(sep);
        if (idx <= 0)
            continue;
        appendQueryVariants(queries, seen, trimmed.left(idx).trimmed());
        appendQueryVariants(queries, seen, trimmed.mid(idx + 1).trimmed());
    }

    // Широкий запрос по франшизе — в конце, чтобы не остановиться на чужих релизах.
    if (!base.isEmpty() && base.length() >= 4)
        appendUniqueQuery(queries, seen, base);
}

QStringList buildJacredQueries(const QString &russianTitle, const QString &englishTitle,
                               const QString &japaneseTitle) {
    QStringList queries;
    QSet<QString> seen;

    const QString russian = russianTitle.trimmed();
    if (!russian.isEmpty() && hasCyrillic(russian))
        appendTitleQueries(&queries, &seen, russian);

    const QString english = englishTitle.trimmed();
    if (!english.isEmpty() && isLatinTitle(english))
        appendTitleQueries(&queries, &seen, english);

    const QString japanese = japaneseTitle.trimmed();
    if (!japanese.isEmpty() && hasCjk(japanese))
        appendTitleQueries(&queries, &seen, japanese);

    return queries;
}

QStringList jacredQueriesFromItem(const QVariantMap &item) {
    QString russian = item.value(QStringLiteral("title")).toString();
    if (!hasCyrillic(russian))
        russian.clear();

    QString english = item.value(QStringLiteral("englishTitle")).toString().trimmed();
    if (english.isEmpty()) {
        const QString fallback = item.value(QStringLiteral("originalTitle")).toString().trimmed();
        if (isLatinTitle(fallback))
            english = fallback;
    }

    const QString japanese = item.value(QStringLiteral("japaneseTitle")).toString();
    return buildJacredQueries(russian, english, japanese);
}

QStringList hentaiTorrentQueries(const QVariantMap &item) {
    // Sukebei — латиница/японский; русские названия из Shikimori там не встречаются.
    QStringList queries;
    QSet<QString> seen;

    const QString english = item.value(QStringLiteral("englishTitle")).toString().trimmed();
    const QString original = item.value(QStringLiteral("originalTitle")).toString().trimmed();
    const QString japanese = item.value(QStringLiteral("japaneseTitle")).toString().trimmed();

    if (!english.isEmpty() && isLatinTitle(english))
        appendTitleQueries(&queries, &seen, english);
    if (!original.isEmpty() && isLatinTitle(original)
        && original.compare(english, Qt::CaseInsensitive) != 0)
        appendTitleQueries(&queries, &seen, original);
    if (!japanese.isEmpty() && hasCjk(japanese))
        appendTitleQueries(&queries, &seen, japanese);

    return queries;
}

bool isUkrainianRelease(const QString &title) {
    if (title.isEmpty())
        return false;
    QString t = title.toLower();
    QString n = t;
    n.replace(QStringLiteral("і"), QStringLiteral("i"));
    n.replace(QStringLiteral("ї"), QStringLiteral("i"));
    n.replace(QStringLiteral("є"), QStringLiteral("e"));
    static const QStringList markers = {
        QStringLiteral("ukrain"), QStringLiteral("украин"), QStringLiteral("україн"),
        QStringLiteral("ukraine"), QStringLiteral("toloka"), QStringLiteral("анілібрія"),
        QStringLiteral("ukr dub"), QStringLiteral("ukr voice"), QStringLiteral("ukr.ozv"),
        QStringLiteral("укр.озв"), QStringLiteral("укр озв"), QStringLiteral("укр дуб"),
        QStringLiteral("українськ"),
    };
    for (const QString &m : markers) {
        if (n.contains(m) || t.contains(m))
            return true;
    }
    if (n.contains(QStringLiteral(" ukr ")) || n.startsWith(QStringLiteral("ukr ")))
        return true;
    return false;
}

QString normalizedReleaseTitle(const QString &title) {
    return stripParentheses(title).toLower();
}

// Только буквы/цифры, в нижнем регистре — раздачи форматируют "Re:Zero" как
// "Re: Zero", "RE ZERO" и т.п.; сравнение по алфанум-ядру ловит все эти
// варианты, а не только точное "rezero" без пробела.
QString alnumLower(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar &c : text) {
        if (c.isLetterOrNumber())
            out.append(c.toLower());
    }
    return out;
}

int scoreTorrentRelevance(const QString &releaseTitle, const QVariantMap &item) {
    if (releaseTitle.isEmpty())
        return -1000;

    const int year = item.value(QStringLiteral("year")).toInt();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    if (!matchesYear(releaseTitle, year) || !matchesKind(releaseTitle, kind)
        || isUkrainianRelease(releaseTitle))
        return -1000;

    const QString rt = releaseTitle.toLower();
    const QString norm = normalizedReleaseTitle(releaseTitle);

    const QString title = item.value(QStringLiteral("title")).toString();
    const QString originalTitle = item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = item.value(QStringLiteral("englishTitle")).toString();

    QString version = extractVersionToken(title);
    if (version.isEmpty())
        version = extractVersionToken(originalTitle);
    if (version.isEmpty())
        version = extractVersionToken(englishTitle);

    int score = 0;

    if (!version.isEmpty()) {
        if (rt.contains(version))
            score += 55;
        else
            score -= 35;
        QString versionNoDot = version;
        versionNoDot.remove(QLatin1Char('.'));
        if (!versionNoDot.isEmpty() && norm.contains(versionNoDot))
            score += 5;
    }

    auto scoreSubtitle = [&](const QString &src) {
        // >= 4 — та же поправка, что в appendTitleQueries: "Re:Zero" не
        // "Заголовок: Подзаголовок".
        const int colon = src.indexOf(QLatin1Char(':'));
        if (colon < 4)
            return;
        const QString sub = normalizedReleaseTitle(src.mid(colon + 1));
        if (sub.length() < 4)
            return;
        if (norm.contains(sub))
            score += 28;
    };
    scoreSubtitle(title);
    scoreSubtitle(englishTitle);
    scoreSubtitle(originalTitle);

    const QString base = !extractFranchiseBase(title).isEmpty()
        ? extractFranchiseBase(title)
        : extractFranchiseBase(englishTitle);
    if (!base.isEmpty() && alnumLower(rt).contains(alnumLower(base)))
        score += 18;

    if (rt.contains(QStringLiteral("neon genesis")) || rt.contains(QStringLiteral("shinseiki"))
        || rt.contains(QStringLiteral("evangelion")) || rt.contains(QStringLiteral("евангелион")))
        score += 8;

    if (kind == QStringLiteral("movie")
        && (rt.contains(QStringLiteral("movie")) || rt.contains(QStringLiteral("фильм"))))
        score += 6;

    if (year > 0) {
        static const QRegularExpression yearRe(R"((?:19|20)\d{2})");
        if (yearRe.match(rt).hasMatch())
            score += 8;
    }

    return score;
}

QVariantList rankTorrentResults(const QVariantList &raw, const QVariantMap &item, int minScore) {
    QHash<QString, QVariantMap> byMagnet;
    QHash<QString, int> scores;

    for (const QVariant &tv : raw) {
        const QVariantMap t = tv.toMap();
        const QString magnet = t.value(QStringLiteral("magnet")).toString();
        if (magnet.isEmpty())
            continue;
        const int score = scoreTorrentRelevance(t.value(QStringLiteral("title")).toString(), item);
        if (score < minScore)
            continue;
        const auto it = scores.constFind(magnet);
        if (it == scores.constEnd() || score > it.value()) {
            byMagnet[magnet] = t;
            scores[magnet] = score;
        }
    }

    QVariantList out;
    out.reserve(byMagnet.size());
    for (auto it = byMagnet.constBegin(); it != byMagnet.constEnd(); ++it) {
        QVariantMap row = it.value();
        row[QStringLiteral("_relevance")] = scores.value(it.key());
        out << row;
    }

    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap am = a.toMap();
        const QVariantMap bm = b.toMap();
        const int sa = am.value(QStringLiteral("_relevance")).toInt();
        const int sb = bm.value(QStringLiteral("_relevance")).toInt();
        if (sa != sb)
            return sa > sb;
        return am.value(QStringLiteral("seeders")).toInt() > bm.value(QStringLiteral("seeders")).toInt();
    });

    for (QVariant &v : out) {
        QVariantMap m = v.toMap();
        m.remove(QStringLiteral("_relevance"));
        v = m;
    }
    return out;
}

QVariantList filterJacredResults(const QVariantList &raw, int year, const QString &kind) {
    QVariantList out;
    QSet<QString> seenMagnets;
    for (const QVariant &tv : raw) {
        QVariantMap t = tv.toMap();
        const QString magnet = t.value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        const QString releaseTitle = t.value("title").toString();
        if (!matchesYear(releaseTitle, year) || !matchesKind(releaseTitle, kind)
            || isUkrainianRelease(releaseTitle))
            continue;
        seenMagnets.insert(magnet);
        out << t;
    }
    return out;
}

}

DetailBridge::DetailBridge(QObject *parent) : QObject(parent) {}

void DetailBridge::attachPlaybackController(QObject *controller) {
    m_playback = qobject_cast<PlaybackController *>(controller);
    if (!m_playback) {
        emit error("DetailBridge: переданный объект не является PlaybackController");
        return;
    }
    // Smash + авто-переход на следующую серию: PlaybackController не умеет
    // сам резолвить озвучку Kodik/CVH — при EOF на смэш-торренте он просит
    // нас повторить playSmashMixed целиком (видео+звук), а не только видео.
    connect(m_playback, &PlaybackController::smashNextEpisodeNeeded, this,
            [this](int episode, const QString &audioTranslationId) {
                playSmashMixed(episode, audioTranslationId);
            });
}

QString DetailBridge::currentStatus() const {
    return StatusStore::instance()->currentStatus(m_item.value("id").toString());
}

void DetailBridge::setSourceStatus(const QString &source, const QString &state, const QString &message) {
    QVariantMap entry;
    entry[QStringLiteral("state")] = state;
    entry[QStringLiteral("message")] = message;
    if (m_sourceStatus.value(source).toMap() == entry)
        return;
    m_sourceStatus[source] = entry;
    emit sourceStatusChanged();
}

void DetailBridge::load(const QVariant &item) {
    m_item = item.toMap();
    QString id = m_item.value("id").toString();
    if (id.isEmpty())
        return;

    m_anilibriaRelease = QVariantMap();
    m_anilibriaAvailable = false;
    m_animegoId.clear();
    m_lastTorrentMagnet.clear();
    m_jacredTorrents.clear();
    m_jacredSearchDone = false;
    m_deferredTorrentSearch = false;
    m_cvhReady = false;
    m_cvhInFlight = false;
    m_cvhRetryQueued = false;
    m_kodikReady = false;
    m_animetkaReady = false;
    m_cvhTranslations.clear();
    m_kodikTranslations.clear();
    m_animetkaTranslations.clear();
    m_hentasisTranslations.clear();
    m_anistarTranslations.clear();
    m_anistarTorrents.clear();
    m_hentasisReady = false;
    m_anistarReady = false;
    ++m_cvhGen;
    ++m_kodikGen;
    ++m_animetkaGen;
    ++m_hentaiSourcesGen;
    ++m_loadGen;
    m_sourceStatus.clear();
    emit sourceStatusChanged();
    emit statusChanged();

    QVariantMap progress = HistoryManager::instance()->loadProgress(id);
    QVariantMap torrentInfo = StatusStore::instance()->getTorrent(id);
    m_lastTorrentMagnet = torrentInfo.value("magnet").toString();
    if (progress.value("found").toBool()) {
        emit progressReady(
            progress.value("episode").toInt(),
            progress.value("translationId").toString(),
            m_lastTorrentMagnet);
    }

    const bool hentai = isHentaiItem(m_item);
    searchTorrents();
    if (hentai) {
        loadKodik();
        loadAnimetka();
        loadHentaiSources();
    } else {
        loadKodik();
        loadAnimetka();
        // CVH стартует сразу с данными карточки (title/originalTitle/kind/year
        // уже есть из каталога) — раньше он ждал полного ответа Shikimori
        // getDetails (1-3+ сек), из-за чего озвучки CVH появлялись заметно
        // позже Kodik. После getDetails/AniList остаётся только retry с
        // обогащёнными названиями, если первая попытка не нашла (см. ниже).
        loadCvh();
    }

    QPointer<DetailBridge> self(this);
    // Поколение текущей загрузки — все колбэки ниже (Shikimori/AniList/
    // AniLibria/related) режутся по нему, чтобы при быстром открытии
    // тайтла B результаты тайтла A не применялись к нему.
    const int loadGen = m_loadGen;
    m_shikimori.getDetails(id, [self, id, loadGen](QVariantMap details, QString err) {
        if (!self || loadGen != self->m_loadGen)
            return;
        if (!err.isEmpty() || details.isEmpty())
            return;
        for (auto it = details.constBegin(); it != details.constEnd(); ++it)
            self->m_item[it.key()] = it.value();
        emit self->detailsReady(self->m_item);
        if (self->m_playback)
            self->m_playback->openTitle(id, self->m_item.value("episodes").toInt());
        // Retry CVH с деталями Shikimori только если ранний запуск (см. load())
        // ещё не нашёл озвучки — не дёргаем Animego повторно без нужды.
        if (!isHentaiItem(self->m_item) && !self->m_cvhReady)
            self->loadCvh();

        const int malId = self->m_item.value(QStringLiteral("malId")).toInt();
        if (malId <= 0)
            return;

        // Кэшируем AniList hero-баннер в фоне ПРЯМО СЕЙЧАС, пока пользователь
        // открыл тайтл и смотрит серию — а не только когда он уже показан в
        // hero на Главной. Раньше баннер докачивался лишь в момент показа на
        // Главной (часто впервые при следующем заходе в приложение), и пока
        // шла загрузка/была неудачной (см. "download stalled" в логе), hero
        // молча подменялся постером Shikimori. Так к моменту возврата на
        // Главную баннер уже почти наверняка на диске.
        const QString existingBanner = self->m_item.value(QStringLiteral("heroBanner")).toString();
        if (!existingBanner.isEmpty()) {
            PosterCache::instance()->requestPriority(existingBanner);
        } else {
            const QString title = self->m_item.value(QStringLiteral("title")).toString();
            const QString originalTitle = self->m_item.value(QStringLiteral("originalTitle")).toString();
            self->m_anilist.fetchBanner(malId, title, originalTitle, [self, loadGen](QString bannerUrl) {
                if (!self || loadGen != self->m_loadGen || bannerUrl.isEmpty())
                    return;
                self->m_item[QStringLiteral("heroBanner")] = bannerUrl;
                PosterCache::instance()->requestPriority(bannerUrl);
                // Баннер приходит асинхронно, позже основного detailsReady — без
                // повторного эмита QML-сторона (heroBanner-фон в шапке деталей)
                // никогда не узнаёт, что он появился, и остаётся без широкого
                // фона. Переприсваивание safe — тот же паттерн, что и один emit.
                emit self->detailsReady(self->m_item);
            });
        }

        self->m_anilist.fetchTitles(malId, [self, loadGen](AniListClient::MediaTitles titles) {
            if (!self || loadGen != self->m_loadGen)
                return;
            bool enriched = false;
            if (!titles.english.isEmpty()
                && self->m_item.value(QStringLiteral("englishTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("englishTitle")] = titles.english;
                enriched = true;
            }
            if (!titles.native.isEmpty()
                && self->m_item.value(QStringLiteral("japaneseTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("japaneseTitle")] = titles.native;
                enriched = true;
            }
            if (!titles.romaji.isEmpty()
                && self->m_item.value(QStringLiteral("originalTitle")).toString().trimmed().isEmpty()) {
                self->m_item[QStringLiteral("originalTitle")] = titles.romaji;
                enriched = true;
            }
            if (enriched && !self->m_cvhReady)
                self->loadCvh();
            if (isHentaiItem(self->m_item) && enriched) {
                self->loadHentaiSources();
                if (self->m_jacredTorrents.isEmpty()) {
                    if (!self->m_jacredSearchDone)
                        self->m_deferredTorrentSearch = true;
                    else
                        self->searchTorrents();
                }
            }
            if (!enriched || !self->m_jacredTorrents.isEmpty())
                return;
            if (!self->m_jacredSearchDone) {
                self->m_deferredTorrentSearch = true;
                return;
            }
            self->searchTorrents();
        });
    });

    if (!hentai) {
        m_anilibria.findRelease(
            m_item.value(QStringLiteral("title")).toString(),
            m_item.value(QStringLiteral("originalTitle")).toString(),
            m_item.value(QStringLiteral("kind")).toString(),
            m_item.value(QStringLiteral("year")).toInt(),
            [self, loadGen](QVariantMap release, QString err) {
        if (!self || loadGen != self->m_loadGen)
            return;
        const QString title = self->m_item.value(QStringLiteral("title")).toString();
        if (!err.isEmpty())
            qWarning("DetailBridge: AniLibria failed for '%s' — %s", qUtf8Printable(title), qUtf8Printable(err));
        else if (release.isEmpty())
            qWarning("DetailBridge: AniLibria not found for '%s'", qUtf8Printable(title));
        else
            qInfo("DetailBridge: AniLibria ready for '%s' (id=%d)", qUtf8Printable(title), release.value("id").toInt());
        self->m_anilibriaRelease = release;
        self->m_anilibriaAvailable = !release.isEmpty();
        emit self->anilibriaReady(self->m_anilibriaAvailable);
        if (self->m_anilibriaAvailable) {
            int maxEpisode = 0;
            for (const QVariant &ev : AniLibriaClient::getEpisodes(self->m_anilibriaRelease))
                maxEpisode = qMax(maxEpisode, ev.toMap().value("episode").toInt());
            emit self->anilibriaEpisodesReady(maxEpisode);
        }
        self->emitMergedTorrents();
        });
    } else {
        m_anilibriaAvailable = false;
        emit anilibriaReady(false);
    }

    m_shikimori.getRelated(id, [self, loadGen](QVariantList items, QString err) {
        if (self && loadGen == self->m_loadGen && err.isEmpty())
            emit self->relatedReady(items);
    });
}

void DetailBridge::loadCvh() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    // Поиск уже идёт — не перезапускаем (Animego чувствителен к частоте
    // запросов), а откладываем retry до завершения текущей попытки.
    if (m_cvhInFlight) {
        m_cvhRetryQueued = true;
        return;
    }
    m_cvhInFlight = true;
    setSourceStatus(QStringLiteral("cvh"), QStringLiteral("loading"), QString());

    ++m_cvhGen;
    const int gen = m_cvhGen;

    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString().trimmed();
    const QString kind = m_item.value(QStringLiteral("kind")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    QPointer<DetailBridge> self(this);
    m_animego.loadTranslations(title, originalTitle, englishTitle, kind, year,
                               [self, id, gen](QVariantList list, QString err) {
                                   if (!self || gen != self->m_cvhGen)
                                       return;
                                   self->m_cvhInFlight = false;
                                   if (!err.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("error"), err);
                                       qWarning("DetailBridge: CVH failed for %s — %s", qUtf8Printable(id),
                                                qUtf8Printable(err));
                                       // Не шлём в общий statusText — это бы показывало ошибку CVH,
                                       // даже когда пользователь выбрал другой источник (Kodik/AniLibria/
                                       // торрент). Состояние "озвучки не найдены" уже отображается
                                       // отдельным индикатором под кнопкой CVH, видимым только когда
                                       // CVH выбран — см. DetailView.qml.
                                   } else if (list.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("empty"), QString());
                                       qWarning("DetailBridge: CVH empty translations for %s", qUtf8Printable(id));
                                   } else {
                                       self->setSourceStatus(QStringLiteral("cvh"), QStringLiteral("ok"), QString());
                                       self->m_cvhReady = true;
                                       self->m_animegoId = list.first().toMap().value(QStringLiteral("animegoId")).toString();
                                       qInfo("DetailBridge: CVH ready for %s (animego=%s, %d voices)",
                                             qUtf8Printable(id), qUtf8Printable(self->m_animegoId), list.size());
                                   }
                                   self->m_cvhTranslations = list;
                                   self->emitMergedTranslations();
                                   // Отложенный retry (getDetails/AniList успели обогатить
                                   // названия, пока шёл первый поиск) — только если ничего
                                   // не нашли; повторный поиск возьмёт свежий m_item.
                                   if (self->m_cvhRetryQueued) {
                                       self->m_cvhRetryQueued = false;
                                       if (!self->m_cvhReady)
                                           self->loadCvh();
                                   }
                               });
}

void DetailBridge::loadKodik() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    ++m_kodikGen;
    const int gen = m_kodikGen;
    setSourceStatus(QStringLiteral("kodik"), QStringLiteral("loading"), QString());

    QPointer<DetailBridge> self(this);
    m_kodik.loadTranslations(id, [self, id, gen](QVariantList list, QString err) {
        if (!self || gen != self->m_kodikGen)
            return;
        if (!err.isEmpty()) {
            self->setSourceStatus(QStringLiteral("kodik"), QStringLiteral("error"), err);
            qWarning("DetailBridge: Kodik failed for %s — %s", qUtf8Printable(id), qUtf8Printable(err));
        } else if (list.isEmpty()) {
            self->setSourceStatus(QStringLiteral("kodik"), QStringLiteral("empty"), QString());
            qWarning("DetailBridge: Kodik empty translations for %s", qUtf8Printable(id));
        } else {
            self->m_kodikReady = true;
            qInfo("DetailBridge: Kodik ready for %s (%d voices)", qUtf8Printable(id), list.size());
        }
        // Префиксуем id "kodik_", чтобы play()/QML отличали озвучку Kodik от
        // CVH ("cvh_...") при общем списке translationsReady — оба источника
        // показываются и выбираются параллельно, не вытесняя друг друга.
        QVariantList prefixed;
        for (const QVariant &v : list) {
            QVariantMap m = v.toMap();
            m[QStringLiteral("id")] = QStringLiteral("kodik_") + m.value(QStringLiteral("id")).toString();
            prefixed << m;
        }
        self->m_kodikTranslations = prefixed;
        self->emitMergedTranslations();
    });
}

void DetailBridge::loadAnimetka() {
    const QString id = m_item.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return;

    ++m_animetkaGen;
    const int gen = m_animetkaGen;
    setSourceStatus(QStringLiteral("animetka"), QStringLiteral("loading"), QString());

    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    QPointer<DetailBridge> self(this);
    m_animetka.loadTranslations(id, title, originalTitle, englishTitle, year,
                                [self, id, gen](QVariantList list, QString err) {
                                    if (!self || gen != self->m_animetkaGen)
                                        return;
                                    if (!err.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("animetka"), QStringLiteral("error"), err);
                                        qWarning("DetailBridge: Animetka failed for %s — %s",
                                                 qUtf8Printable(id), qUtf8Printable(err));
                                    } else if (list.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("animetka"), QStringLiteral("empty"), QString());
                                        qWarning("DetailBridge: Animetka empty for %s", qUtf8Printable(id));
                                    } else {
                                        self->m_animetkaReady = true;
                                        qInfo("DetailBridge: Animetka ready for %s (%d voices)",
                                              qUtf8Printable(id), list.size());
                                    }
                                    // id уже с префиксом animetka_ из клиента
                                    self->m_animetkaTranslations = list;
                                    self->emitMergedTranslations();
                                });
}

void DetailBridge::emitMergedTranslations() {
    emit translationsReady(m_cvhTranslations + m_kodikTranslations + m_animetkaTranslations
                           + m_hentasisTranslations + m_anistarTranslations);
}

void DetailBridge::loadHentaiSources() {
    const QString title = m_item.value(QStringLiteral("title")).toString();
    const QString originalTitle = m_item.value(QStringLiteral("originalTitle")).toString();
    const QString englishTitle = m_item.value(QStringLiteral("englishTitle")).toString();
    const QString japaneseTitle = m_item.value(QStringLiteral("japaneseTitle")).toString();
    const int year = m_item.value(QStringLiteral("year")).toInt();

    ++m_hentaiSourcesGen;
    const int gen = m_hentaiSourcesGen;
    setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("loading"), QString());
    setSourceStatus(QStringLiteral("anistar"), QStringLiteral("loading"), QString());

    QPointer<DetailBridge> self(this);
    m_hentasis.loadTranslations(title, originalTitle, englishTitle, japaneseTitle, year,
                                [self, gen](QVariantList list, QString err) {
                                    if (!self || gen != self->m_hentaiSourcesGen)
                                        return;
                                    if (!err.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("error"), err);
                                        qWarning("DetailBridge: Hentasis failed — %s", qUtf8Printable(err));
                                    } else if (list.isEmpty()) {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("empty"), QString());
                                        qWarning("DetailBridge: Hentasis not found");
                                    } else {
                                        self->setSourceStatus(QStringLiteral("hentasis"), QStringLiteral("ok"), QString());
                                        qInfo("DetailBridge: Hentasis ready (%d tracks)", list.size());
                                    }
                                    self->m_hentasisReady = !list.isEmpty();
                                    self->m_hentasisTranslations = list;
                                    self->emitMergedTranslations();
                                });

    m_anistar.loadTranslations(title, originalTitle, englishTitle, japaneseTitle, year,
                               [self, gen, title, originalTitle, englishTitle, japaneseTitle, year](
                                   QVariantList list, QString err) {
                                   if (!self || gen != self->m_hentaiSourcesGen)
                                       return;
                                   if (!err.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("error"), err);
                                       qWarning("DetailBridge: AniStar stream failed — %s", qUtf8Printable(err));
                                   } else if (list.isEmpty()) {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("empty"), QString());
                                       qInfo("DetailBridge: AniStar direct stream not found");
                                   } else {
                                       self->setSourceStatus(QStringLiteral("anistar"), QStringLiteral("ok"), QString());
                                       qInfo("DetailBridge: AniStar stream ready (%d tracks)", list.size());
                                   }
                                   self->m_anistarReady = !list.isEmpty();
                                   self->m_anistarTranslations = list;
                                   self->emitMergedTranslations();

                                   self->m_anistar.fetchPageTorrents(
                                       title, originalTitle, englishTitle, japaneseTitle, year,
                                       [self, gen](QVariantList torrents, QString terr) {
                                           if (!self || gen != self->m_hentaiSourcesGen)
                                               return;
                                           if (!terr.isEmpty())
                                               qWarning("DetailBridge: AniStar torrents — %s", qUtf8Printable(terr));
                                           self->m_anistarTorrents.clear();
                                           for (const QVariant &tv : torrents) {
                                               const QVariantMap t = tv.toMap();
                                               if (!t.value(QStringLiteral("magnet")).toString().isEmpty())
                                                   self->m_anistarTorrents << t;
                                           }
                                           if (!self->m_anistarTorrents.isEmpty())
                                               qInfo("DetailBridge: AniStar torrents %d",
                                                     self->m_anistarTorrents.size());
                                           if (self->m_jacredSearchDone)
                                               self->emitMergedTorrents();
                                       });
                               });
}

void DetailBridge::emitMergedTorrents() {
    if (!m_jacredSearchDone)
        return;

    const QString title = m_item.value("title").toString();
    const int year = m_item.value("year").toInt();
    const QString kind = m_item.value("kind").toString();

    QVariantList combined;
    QSet<QString> seenMagnets;
    for (const QVariant &tv : AniLibriaClient::getTorrents(m_anilibriaRelease)) {
        QVariantMap t = tv.toMap();
        QString label = t.value("label").toString();
        if (label.isEmpty())
            label = title;
        if (!matchesYear(label, year) || !matchesKind(label, kind))
            continue;
        const QString magnet = t.value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        QVariantMap out;
        out["title"] = label;
        out["magnet"] = magnet;
        const double sizeGb = t.value("sizeGb").toDouble();
        out["size"] = sizeGb > 0 ? QString::number(sizeGb, 'f', 2) + " ГБ" : "?";
        out["seeders"] = t.value("seeders");
        out["tracker"] = "AniLibria";
        seenMagnets.insert(magnet);
        combined << out;
    }

    for (const QVariant &tv : m_jacredTorrents) {
        const QString magnet = tv.toMap().value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        seenMagnets.insert(magnet);
        combined << tv;
    }

    for (const QVariant &tv : m_anistarTorrents) {
        const QString magnet = tv.toMap().value("magnet").toString();
        if (magnet.isEmpty() || seenMagnets.contains(magnet))
            continue;
        seenMagnets.insert(magnet);
        combined << tv;
    }

    const QString cacheId = m_item.value("id").toString();
    if (!cacheId.isEmpty())
        g_torrentSearchCache[cacheId] = combined;

    emit torrentsLoading(false);
    emit torrentsReady(combined);
}

void DetailBridge::searchTorrents() {
    const QString cacheId = m_item.value("id").toString();
    if (!cacheId.isEmpty() && g_torrentSearchCache.contains(cacheId)) {
        m_jacredSearchDone = true;
        emit torrentsLoading(false);
        emit torrentsReady(g_torrentSearchCache.value(cacheId));
        return;
    }

    const bool hentai = isHentaiItem(m_item);
    const QStringList queries = hentai ? hentaiTorrentQueries(m_item) : jacredQueriesFromItem(m_item);
    if (queries.isEmpty()) {
        m_jacredTorrents.clear();
        m_jacredSearchDone = true;
        if (hentai)
            m_deferredTorrentSearch = true;
        emit torrentsLoading(false);
        emit torrentsReady(QVariantList());
        return;
    }

    ++m_torrentSearchGen;
    const int searchGen = m_torrentSearchGen;
    const int minScore = hentai ? kTorrentMinScoreHentai : kTorrentMinScoreDefault;

    emit torrentsLoading(true);
    m_jacredSearchDone = false;
    m_jacredTorrents.clear();

    const int year = m_item.value("year").toInt();
    const QString kind = m_item.value("kind").toString();
    const QVariantMap itemSnapshot = m_item;
    auto accumulated = std::make_shared<QVariantList>();

    // Сводка по всему поиску (не по одному варианту) — чтобы на практике
    // увидеть, не растягивается ли worst-case на нестандартных названиях
    // из-за backoff'а на 429 (см. обсуждение: 3 повтора одного варианта —
    // уже 2+4+8с, а если таких вариантов несколько подряд — может набежать
    // намного больше, чем было до этого изменения).
    auto searchClock = std::make_shared<QElapsedTimer>();
    searchClock->start();
    auto total429 = std::make_shared<int>(0);

    QPointer<DetailBridge> self(this);
    auto finishSearch = [self, searchGen, accumulated, minScore, searchClock, total429, title = m_item.value("title").toString()]() {
        qInfo("DetailBridge: поиск торрентов для \"%s\" завершён за %lldмс, 429 поймано: %d",
              qUtf8Printable(title), searchClock->elapsed(), *total429);
        if (!self || searchGen != self->m_torrentSearchGen)
            return;
        self->m_jacredTorrents = rankTorrentResults(*accumulated, self->m_item, minScore);
        self->m_jacredSearchDone = true;
        if (self->m_deferredTorrentSearch) {
            self->m_deferredTorrentSearch = false;
            self->searchTorrents();
            return;
        }
        self->emitMergedTorrents();
    };

    // retry429 — счётчик повторов ТЕКУЩЕГО варианта названия именно из-за 429
    // (rate limit), отдельно от обычного перехода к следующему варианту.
    // При 429 пере-запрашиваем тот же вариант с растущей паузой (backoff),
    // вместо того чтобы тратить его молча как "0 результатов" и переходить
    // дальше — иначе на всплеске нагрузки (несколько параллельных поисков)
    // легко потерять реально существующие раздачи только из-за бана.
    auto runQueryAt = std::make_shared<std::function<void(int, int)>>();
    *runQueryAt = [self, queries, runQueryAt, hentai, year, kind, searchGen, itemSnapshot, accumulated,
                   finishSearch, minScore, total429](int index, int retry429) {
        if (!self || searchGen != self->m_torrentSearchGen)
            return;
        if (index >= queries.size()) {
            finishSearch();
            return;
        }

        const auto onResults = [self, runQueryAt, index, retry429, searchGen, itemSnapshot, accumulated,
                                finishSearch, hentai, year, kind, minScore, queries, total429]
                               (QVariantList results, QString err, int httpStatus) {
            Q_UNUSED(err);
            if (!self || searchGen != self->m_torrentSearchGen)
                return;

            constexpr int kMax429Retries = 3;
            if (httpStatus == 429)
                ++(*total429);
            if (httpStatus == 429 && retry429 < kMax429Retries) {
                const int backoffMs = 2000 * (1 << retry429) + jacredJitterMs(0, 500);
                qWarning("JacRed/Sukebei: HTTP 429 (rate limit) для \"%s\" — backoff %dms, повтор %d/%d",
                         qUtf8Printable(queries.at(index)), backoffMs, retry429 + 1, kMax429Retries);
                QTimer::singleShot(backoffMs, [self, runQueryAt, index, retry429, searchGen]() {
                    if (!self || searchGen != self->m_torrentSearchGen)
                        return;
                    (*runQueryAt)(index, retry429 + 1); // тот же вариант, не следующий
                });
                return;
            }
            if (httpStatus == 429) {
                qWarning("JacRed/Sukebei: 429 после %d попыток для \"%s\" — пропускаем вариант",
                         kMax429Retries, qUtf8Printable(queries.at(index)));
            }

            if (hentai) {
                for (const QVariant &tv : results)
                    accumulated->append(tv);
            } else {
                const QVariantList filtered = filterJacredResults(results, year, kind);
                for (const QVariant &tv : filtered)
                    accumulated->append(tv);
            }

            const QVariantList ranked = rankTorrentResults(*accumulated, itemSnapshot, minScore);
            int strongMatches = 0;
            const int strongThreshold = hentai ? 20 : 45;
            for (const QVariant &tv : ranked) {
                if (scoreTorrentRelevance(tv.toMap().value(QStringLiteral("title")).toString(), itemSnapshot)
                    >= strongThreshold)
                    ++strongMatches;
            }
            // Если самый первый (обычно самый точный) запрос уже дал уверенное
            // совпадение — незачем ждать второй ради подстраховки: именно это
            // лишнее ожидание и создавало разницу с сайтом JacRed, где
            // пользователь делает ровно один запрос сам.
            if (strongMatches >= 1) {
                finishSearch();
                return;
            }
            // Пауза перед следующим запросом — см. комментарий у jacredJitterMs().
            QTimer::singleShot(jacredJitterMs(700, 1100), [self, runQueryAt, index, searchGen]() {
                if (!self || searchGen != self->m_torrentSearchGen)
                    return;
                (*runQueryAt)(index + 1, 0);
            });
        };

        if (hentai)
            self->m_sukebei.search(queries.at(index), onResults);
        else
            self->m_jacred.search(queries.at(index), onResults);
    };
    (*runQueryAt)(0, 0);
}

void DetailBridge::selectTorrent(const QString &magnet) {
    m_lastTorrentMagnet = magnet;
}

void DetailBridge::setStatus(const QString &status) {
    QString id = m_item.value("id").toString();
    if (id.isEmpty())
        return;
    if (status.isEmpty()) {
        StatusStore::instance()->removeStatus(id);
    } else {
        const QString poster = ShikimoriClient::bestPosterUrl(
            m_item.value("poster").toString(), m_item.value("posterHd").toString());
        StatusStore::instance()->setStatus(id, status, m_item.value("title").toString(), poster);
    }
    emit statusChanged();
}

void DetailBridge::play(int episode, const QString &translationId) {
    if (!m_playback) {
        emit error("Плеер не подключён");
        return;
    }
    QString id = m_item.value("id").toString();
    QString title = m_item.value("title").toString();
    QString poster = ShikimoriClient::bestPosterUrl(
        m_item.value("poster").toString(), m_item.value("posterHd").toString());

    // Каждый play() отменяет резолвы предыдущих — иначе быстрый клик
    // "серия 5 -> серия 3" может применить URL пятой серии после третьей
    // (getEpisodeStream асинхронный, порядок ответов не гарантирован).
    const int gen = ++m_playGen;

    if (translationId == "torrent") {
        if (m_lastTorrentMagnet.isEmpty()) {
            emit error("Раздача не выбрана — выбери торрент в списке");
            return;
        }
        StatusStore::instance()->setTorrent(id, QString(), m_lastTorrentMagnet, title, poster);
        m_playback->playTorrentEpisode(m_lastTorrentMagnet, episode, QStringLiteral("torrent"));
    } else if (translationId == "anilibria") {
        if (m_anilibriaRelease.isEmpty()) {
            emit error("Этот тайтл не найден в каталоге AniLibria API");
            return;
        }
        QString url = AniLibriaClient::getEpisodeStream(m_anilibriaRelease, episode);
        if (url.isEmpty()) {
            emit error(QString("Серия %1 недоступна в AniLibria API").arg(episode));
            return;
        }
        m_playback->playDirectUrl(url, episode, false, QStringLiteral("anilibria"));
    } else if (translationId.startsWith(QStringLiteral("cvh_"))) {
        if (m_animegoId.isEmpty()) {
            emit error(QStringLiteral("Озвучки CVH ещё не загружены"));
            return;
        }
        QPointer<DetailBridge> self(this);
        m_animego.getEpisodeStream(m_animegoId, episode, translationId,
                                   [self, episode, translationId, gen](QString url, QString err) {
                                       if (!self || gen != self->m_playGen)
                                           return;
                                       if (url.isEmpty()) {
                                           emit self->error(err.isEmpty()
                                                          ? QString("Не удалось получить ссылку на серию %1")
                                                                .arg(episode)
                                                          : err);
                                           return;
                                       }
                                       qInfo("DetailBridge: CVH stream ep=%d direct host=%s",
                                             episode, qUtf8Printable(QUrl(url).host()));
                                       // CVH CDN (okcdn) — напрямую; прокси нужен только для animego.org.
                                       if (self->m_playback)
                                           self->m_playback->playDirectUrl(url, episode, false, translationId);
                                   });
    } else if (translationId.startsWith(QStringLiteral("hentasis_"))) {
        QPointer<DetailBridge> self(this);
        m_hentasis.getEpisodeStream(translationId, episode,
                                    [self, episode, translationId, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        if (url.isEmpty()) {
                                            emit self->error(err.isEmpty()
                                                           ? QString("Серия %1 недоступна (Hentasis)").arg(episode)
                                                           : err);
                                            return;
                                        }
                                        qInfo("DetailBridge: Hentasis stream ep=%d host=%s",
                                              episode, qUtf8Printable(QUrl(url).host()));
                                        if (self->m_playback)
                                            self->m_playback->playDirectUrl(url, episode, false, translationId);
                                    });
    } else if (translationId.startsWith(QStringLiteral("anistar_"))) {
        QPointer<DetailBridge> self(this);
        m_anistar.getEpisodeStream(translationId, episode,
                                   [self, episode, translationId, gen](QString url, QString err) {
                                       if (!self || gen != self->m_playGen)
                                           return;
                                       if (url.isEmpty()) {
                                           emit self->error(err.isEmpty()
                                                          ? QString("Серия %1 недоступна (AniStar)").arg(episode)
                                                          : err);
                                           return;
                                       }
                                       qInfo("DetailBridge: AniStar stream ep=%d host=%s",
                                             episode, qUtf8Printable(QUrl(url).host()));
                                       if (self->m_playback)
                                           self->m_playback->playDirectUrl(url, episode, true, translationId);
                                   });
    } else if (translationId.startsWith(QStringLiteral("kodik_"))) {
        const QString rawId = translationId.mid(6); // strlen("kodik_")
        QPointer<DetailBridge> self(this);
        m_kodik.getEpisodeStream(id, episode, rawId,
                                 [self, episode, translationId, gen](QString url, QString err) {
                                     if (!self || gen != self->m_playGen)
                                         return;
                                     if (url.isEmpty()) {
                                         emit self->error(err.isEmpty()
                                                        ? QString("Не удалось получить ссылку на серию %1 (Kodik)")
                                                              .arg(episode)
                                                        : err);
                                         return;
                                     }
                                     qInfo("DetailBridge: Kodik stream ep=%d host=%s",
                                           episode, qUtf8Printable(QUrl(url).host()));
                                     // Kodik геоблокирован — в отличие от CVH/okcdn, поток
                                     // нужно качать через прокси (useProxy=true).
                                     if (self->m_playback)
                                         self->m_playback->playDirectUrl(url, episode, true, translationId);
                                 });
    } else if (translationId.startsWith(QStringLiteral("animetka_"))) {
        QString tid;
        QString quality;
        AnimetkaClient::parseTranslationId(translationId, &tid, &quality);
        QPointer<DetailBridge> self(this);
        m_animetka.getEpisodeStream(translationId, episode, quality,
                                    [self, id, episode, translationId, tid, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        // Прямой HLS / animetka video-proxy — без Kodik-прокси.
                                        if (!url.isEmpty() && !url.startsWith(QLatin1String("kodik_fallback:"))) {
                                            qInfo("DetailBridge: Animetka stream ep=%d host=%s",
                                                  episode, qUtf8Printable(QUrl(url).host()));
                                            if (self->m_playback)
                                                self->m_playback->playDirectUrl(url, episode, false, translationId);
                                            return;
                                        }
                                        if (!err.isEmpty()) {
                                            emit self->error(err);
                                            return;
                                        }
                                        // Fallback: тот же translation id в Kodik (часто совпадает)
                                        const QString kodikTid = url.startsWith(QLatin1String("kodik_fallback:"))
                                            ? url.mid(QStringLiteral("kodik_fallback:").size())
                                            : tid;
                                        qInfo("DetailBridge: Animetka → Kodik fallback tid=%s ep=%d",
                                              qUtf8Printable(kodikTid), episode);
                                        self->m_kodik.getEpisodeStream(
                                            id, episode, kodikTid,
                                            [self, episode, translationId, gen](QString kurl, QString kerr) {
                                                if (!self || gen != self->m_playGen)
                                                    return;
                                                if (kurl.isEmpty()) {
                                                    emit self->error(
                                                        kerr.isEmpty()
                                                            ? QStringLiteral("Animetka/Kodik: нет потока для серии %1")
                                                                  .arg(episode)
                                                            : kerr);
                                                    return;
                                                }
                                                if (self->m_playback)
                                                    self->m_playback->playDirectUrl(kurl, episode, true, translationId);
                                            });
                                    });
    } else {
        emit error(QStringLiteral("Неизвестный источник озвучки"));
    }
}

void DetailBridge::playSmashMixed(int episode, const QString &audioTranslationId) {
    if (!m_playback) {
        emit error("Плеер не подключён");
        return;
    }
    if (m_lastTorrentMagnet.isEmpty()) {
        emit error("Раздача не выбрана — выбери торрент в списке");
        return;
    }
    QString id = m_item.value("id").toString();
    QString title = m_item.value("title").toString();
    QString poster = ShikimoriClient::bestPosterUrl(
        m_item.value("poster").toString(), m_item.value("posterHd").toString());
    const int gen = ++m_playGen;
    StatusStore::instance()->setTorrent(id, QString(), m_lastTorrentMagnet, title, poster);
    m_playback->playTorrentEpisode(m_lastTorrentMagnet, episode, QStringLiteral("torrent"));
    // playTorrentEpisode() сбрасывает хинт — ставим заново, ПОСЛЕ вызова.
    m_playback->setSmashAudioHint(audioTranslationId);

    if (audioTranslationId.isEmpty()) {
        emit error(QStringLiteral("Смэш: озвучка не выбрана — видео играет без звука"));
        return;
    }

    QPointer<DetailBridge> self(this);
    if (audioTranslationId.startsWith(QStringLiteral("cvh_"))) {
        if (m_animegoId.isEmpty()) {
            emit error(QStringLiteral("Озвучки CVH ещё не загружены — видео идёт без Смэш-звука"));
            return;
        }
        m_animego.getEpisodeStream(m_animegoId, episode, audioTranslationId,
                                    [self, episode, gen](QString url, QString err) {
                                        if (!self || gen != self->m_playGen)
                                            return;
                                        if (url.isEmpty()) {
                                            emit self->error(err.isEmpty()
                                                ? QString("Смэш: не удалось получить озвучку CVH для серии %1 — видео без звука")
                                                      .arg(episode)
                                                : err);
                                            return;
                                        }
                                        if (self->m_playback)
                                            self->m_playback->attachExternalAudio(url, false);
                                    });
    } else if (audioTranslationId.startsWith(QStringLiteral("kodik_"))) {
        const QString rawId = audioTranslationId.mid(6); // strlen("kodik_")
        m_kodik.getEpisodeStream(id, episode, rawId,
                                  [self, episode, gen](QString url, QString err) {
                                      if (!self || gen != self->m_playGen)
                                          return;
                                      if (url.isEmpty()) {
                                          emit self->error(err.isEmpty()
                                              ? QString("Смэш: не удалось получить озвучку Kodik для серии %1 — видео без звука")
                                                    .arg(episode)
                                              : err);
                                          return;
                                      }
                                      // Kodik геоблокирован — звук тянем через прокси, как и
                                      // в обычном воспроизведении через Kodik.
                                      if (self->m_playback)
                                          self->m_playback->attachExternalAudio(url, true);
                                  });
    } else {
        emit error(QStringLiteral("Смэш: неизвестный источник озвучки «%1» — видео играет без звука")
                       .arg(audioTranslationId));
    }
}
