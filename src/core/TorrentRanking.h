#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Чистые функции поиска/ранжирования торрентов (JacRed/Sukebei) и построения
// поисковых запросов по названию тайтла. Вынесены из анонимного namespace
// DetailBridge.cpp, чтобы покрыть их unit-тестами (tests/test_torrent_ranking.cpp)
// — все они детерминированы, сеть не нужна.
namespace torrentRanking {

// Эвристики соответствия раздачи (год/тип релиза), порт из Python-версии.
bool matchesYear(const QString &title, int year);
bool looksLikeTvSeriesPack(const QString &title);
bool matchesKind(const QString &title, const QString &kind);
bool hasCyrillic(const QString &text);
bool hasCjk(const QString &text);
bool isLatinTitle(const QString &text);

// Построители вариантов запроса по названию.
void appendUniqueQuery(QStringList *queries, QSet<QString> *seen, const QString &query);
QString stripParentheses(const QString &text);
QString compactSpaces(const QString &text);
QString compactNoSpaces(const QString &text);
QString extractVersionToken(const QString &text);
QString extractFranchiseBase(const QString &text);
void appendQueryVariants(QStringList *queries, QSet<QString> *seen, const QString &query);
void appendTitleQueries(QStringList *queries, QSet<QString> *seen, const QString &primary);

// Списки запросов для JacRed (не-хентай) и Sukebei/AniStar (хентай).
QStringList buildJacredQueries(const QString &russianTitle, const QString &englishTitle,
                               const QString &japaneseTitle);
QStringList jacredQueriesFromItem(const QVariantMap &item);
QStringList hentaiTorrentQueries(const QVariantMap &item);

// Нормализация названия раздачи.
bool isUkrainianRelease(const QString &title);
QString normalizedReleaseTitle(const QString &title);
QString alnumLower(const QString &text);

// Ранжирование: score → rank (dedup по magnet, фильтр minScore, сорт по
// релевантности, затем по сидам) → filter (год/тип/укр. + dedup).
int scoreTorrentRelevance(const QString &releaseTitle, const QVariantMap &item);
QVariantList rankTorrentResults(const QVariantList &raw, const QVariantMap &item, int minScore);
QVariantList filterJacredResults(const QVariantList &raw, int year, const QString &kind);

} // namespace torrentRanking
