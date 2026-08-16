#include "EpisodeParser.h"

#include <QtTest>
#include <QStringList>

// Unit-тесты EpisodeParser — чистый модуль (строки -> int), сеть не нужна.
// Запуск: ctest --test-dir build -R episode_parser  (или ./build/tests/test_episode_parser)
class TestEpisodeParser : public QObject {
    Q_OBJECT
private slots:
    void parseExplicitEpisode();
    void parseBracketNumbers();
    void parseBareNumbers();
    void parseNoise();
    void parseFailures();
    void pickIndex();
};

void TestEpisodeParser::parseExplicitEpisode() {
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - Episode 05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - Эпизод 05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - Серия 05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - Ep.05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - Ep 05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show E05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show e05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show S01E05.mkv")), 5);
}

void TestEpisodeParser::parseBracketNumbers() {
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show [05].mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show (05).mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - 05 - BD.mkv")), 5);
}

void TestEpisodeParser::parseBareNumbers() {
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Kiss X Sis 01 [BDRip 1080p].mkv")), 1);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Title - 05.mkv")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("05.mp4")), 5);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Title 05.mkv")), 5);
}

void TestEpisodeParser::parseNoise() {
    // Разрешения/годы — не серии
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show 1080p.mkv")), -1);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show 2024.mkv")), -1);
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show [720p].mkv")), -1);
    // Номер серии рядом с разрешением — берём серию
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show - 03 [1080p].mkv")), 3);
}

void TestEpisodeParser::parseFailures() {
    QCOMPARE(EpisodeParser::parse(QStringLiteral("Show.mkv")), -1);
    QCOMPARE(EpisodeParser::parse(QString()), -1);
}

void TestEpisodeParser::pickIndex() {
    const QStringList files{QStringLiteral("Show - 01.mkv"), QStringLiteral("Show - 02.mkv"),
                            QStringLiteral("Show - 03.mkv")};
    QCOMPARE(EpisodeParser::pickEpisodeIndex(files, 1), 0);
    QCOMPARE(EpisodeParser::pickEpisodeIndex(files, 2), 1);
    QCOMPARE(EpisodeParser::pickEpisodeIndex(files, 3), 2);
    QCOMPARE(EpisodeParser::pickEpisodeIndex(files, 4), -1);

    // Без номеров в именах — порядковый индекс (один файл = один эпизод)
    const QStringList unnamed{QStringLiteral("part1.mkv"), QStringLiteral("part2.mkv"),
                              QStringLiteral("part3.mkv")};
    QCOMPARE(EpisodeParser::pickEpisodeIndex(unnamed, 2), 1);
    QCOMPARE(EpisodeParser::pickEpisodeIndex(unnamed, 3), 2);

    // Эпизод вне диапазона
    QCOMPARE(EpisodeParser::pickEpisodeIndex(unnamed, 0), -1);
    QCOMPARE(EpisodeParser::pickEpisodeIndex(unnamed, 5), -1);
}

QTEST_APPLESS_MAIN(TestEpisodeParser)
#include "test_episode_parser.moc"
