#include "TorrentRanking.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace torrentRanking {


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

} // namespace torrentRanking
