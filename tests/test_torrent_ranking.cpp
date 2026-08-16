#include "TorrentRanking.h"

#include <QtTest>

using namespace torrentRanking;

// Unit-тесты поиска/ранжирования торрентов (JacRed/Sukebei) — чистые
// функции, сеть не нужна. Запуск: ctest --test-dir build -R torrent_ranking
class TestTorrentRanking : public QObject {
    Q_OBJECT
private slots:
    void scoreBasics();
    void scoreVersionAndFranchise();
    void filterJacred();
    void rankResults();
    void queriesFromItem();
    void queryHelpers();
};

static QVariantMap makeItem(const QString &title, const QString &originalTitle,
                            const QString &englishTitle, const QString &japaneseTitle,
                            int year, const QString &kind) {
    QVariantMap m;
    m[QStringLiteral("title")] = title;
    m[QStringLiteral("originalTitle")] = originalTitle;
    m[QStringLiteral("englishTitle")] = englishTitle;
    m[QStringLiteral("japaneseTitle")] = japaneseTitle;
    m[QStringLiteral("year")] = year;
    m[QStringLiteral("kind")] = kind;
    return m;
}

void TestTorrentRanking::scoreBasics() {
    const QVariantMap item = makeItem(QStringLiteral("Тетрадь смерти"), QStringLiteral("Death Note"),
                                      QStringLiteral("Death Note"), QStringLiteral("デスノート"), 2006,
                                      QStringLiteral("tv"));

    // Пустое название — жёсткий отказ
    QCOMPARE(scoreTorrentRelevance(QString(), item), -1000);
    // Совпадает по франшизе (alnum-ядро) и году нет в названии — положительный скор
    QVERIFY(scoreTorrentRelevance(QStringLiteral("Тетрадь смерти 01 [BDRip 1080p]"), item) > 0);
    // Чужой год в названии — отказ
    QCOMPARE(scoreTorrentRelevance(QStringLiteral("Тетрадь смерти 2022"), item), -1000);
    // Украинская озвучка — отказ
    QCOMPARE(scoreTorrentRelevance(QStringLiteral("Тетрадь смерти [Ukr Dub] 2006"), item), -1000);
    // Тип релиза: фильм вместо ТВ-сериала — отказ
    QCOMPARE(scoreTorrentRelevance(QStringLiteral("Тетрадь смерти фильм"), item), -1000);
}

void TestTorrentRanking::scoreVersionAndFranchise() {
    // Версия 1.11 в названии тайтла даёт сильный буст, если она есть в раздаче
    const QVariantMap item = makeItem(QStringLiteral("Re:Zero 1.11"), QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu"),
                                      QStringLiteral("Re:Zero"), QString(), 2016, QStringLiteral("tv"));
    const int withVersion = scoreTorrentRelevance(QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu 1.11 [BDRip]"), item);
    const int withoutVersion = scoreTorrentRelevance(QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu [BDRip]"), item);
    QVERIFY(withVersion > withoutVersion);

    // Франшиза из короткого первого слова: "Re" + "Zero" → "ReZero"
    QCOMPARE(extractFranchiseBase(QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu")), QStringLiteral("ReZero"));
    QCOMPARE(extractFranchiseBase(QStringLiteral("One Piece")), QStringLiteral("OnePiece"));
}

void TestTorrentRanking::filterJacred() {
    QVariantList raw;
    auto torrent = [](const QString &title, const QString &magnet) {
        QVariantMap t;
        t[QStringLiteral("title")] = title;
        t[QStringLiteral("magnet")] = magnet;
        return t;
    };
    raw << torrent(QStringLiteral("Наруто 01 [1080p]"), QStringLiteral("m1"));
    raw << torrent(QStringLiteral("Наруто 01 [1080p]"), QStringLiteral("m1")); // дубль магнита
    raw << torrent(QStringLiteral("Наруто 2019"), QStringLiteral("m2"));      // не тот год
    raw << torrent(QStringLiteral("Наруто [Ukr Dub]"), QStringLiteral("m3")); // укр.
    raw << torrent(QStringLiteral("Наруто Фильм"), QStringLiteral("m4"));      // фильм вместо TV

    const QVariantList filtered = filterJacredResults(raw, 2022, QStringLiteral("tv"));
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().toMap().value(QStringLiteral("magnet")).toString(), QStringLiteral("m1"));
}

void TestTorrentRanking::rankResults() {
    const QVariantMap item = makeItem(QStringLiteral("Наруто"), QStringLiteral("Naruto"),
                                      QStringLiteral("Naruto"), QString(), 2002, QStringLiteral("tv"));
    QVariantList raw;
    auto torrent = [](const QString &title, const QString &magnet, int seeders) {
        QVariantMap t;
        t[QStringLiteral("title")] = title;
        t[QStringLiteral("magnet")] = magnet;
        t[QStringLiteral("seeders")] = seeders;
        return t;
    };
    // Раздачи называются по-русски (JacRed ищет по русскому названию) —
    // английское имя раздачи к русской базе тайтла не привяжется (скор 0).
    raw << torrent(QStringLiteral("Наруто 01 [BDRip]"), QStringLiteral("m1"), 10);
    raw << torrent(QStringLiteral("Наруто Полная коллекция [BDRip]"), QStringLiteral("m1"), 5);
    // Совсем посторонняя раздача — отсеется по minScore
    raw << torrent(QStringLiteral("Boruto [BDRip]"), QStringLiteral("m2"), 99);

    const QVariantList ranked = rankTorrentResults(raw, item, 10);
    QCOMPARE(ranked.size(), 1);
    QCOMPARE(ranked.first().toMap().value(QStringLiteral("magnet")).toString(), QStringLiteral("m1"));
    // "_relevance" — служебное поле, наружу не уходит
    QVERIFY(!ranked.first().toMap().contains(QStringLiteral("_relevance")));
}

void TestTorrentRanking::queriesFromItem() {
    QVariantMap item;
    item[QStringLiteral("title")] = QStringLiteral("Тетрадь смерти");
    item[QStringLiteral("englishTitle")] = QStringLiteral("Death Note");
    item[QStringLiteral("japaneseTitle")] = QStringLiteral("デスノート");
    item[QStringLiteral("originalTitle")] = QStringLiteral("Death Note");
    item[QStringLiteral("year")] = 2006;
    item[QStringLiteral("kind")] = QStringLiteral("tv");

    const QStringList queries = jacredQueriesFromItem(item);
    QVERIFY(!queries.isEmpty());
    QVERIFY(queries.contains(QStringLiteral("тетрадь смерти")) || queries.contains(QStringLiteral("Тетрадь смерти")));
    QVERIFY(queries.contains(QStringLiteral("death note")) || queries.contains(QStringLiteral("Death Note")));

    // Латиница из originalTitle подхватывается, если englishTitle пуст
    QVariantMap item2 = item;
    item2[QStringLiteral("englishTitle")] = QString();
    const QStringList queries2 = jacredQueriesFromItem(item2);
    QVERIFY(queries2.contains(QStringLiteral("Death Note")) || queries2.contains(QStringLiteral("death note")));
}

void TestTorrentRanking::queryHelpers() {
    // matchesYear: год в названии должен совпадать, без года — пропускаем
    QVERIFY(matchesYear(QStringLiteral("Title 2021"), 2021));
    QVERIFY(!matchesYear(QStringLiteral("Title 2021"), 2020));
    QVERIFY(matchesYear(QStringLiteral("Title"), 2021));

    // matchesKind: "фильм" в названии не подходит для TV
    QVERIFY(!matchesKind(QStringLiteral("Title (фильм)"), QStringLiteral("tv")));
    QVERIFY(matchesKind(QStringLiteral("Title 01"), QStringLiteral("tv")));
    // паки сезонов не подходят для movie
    QVERIFY(!matchesKind(QStringLiteral("Title S01 [TV]"), QStringLiteral("movie")));

    // Киррилица/лат/японский
    QVERIFY(hasCyrillic(QStringLiteral("Наруто")));
    QVERIFY(!hasCyrillic(QStringLiteral("Naruto")));
    QVERIFY(hasCjk(QStringLiteral("デスノート")));
    QVERIFY(isLatinTitle(QStringLiteral("Naruto")));
    QVERIFY(!isLatinTitle(QStringLiteral("Наруто")));
}

QTEST_APPLESS_MAIN(TestTorrentRanking)
#include "test_torrent_ranking.moc"
