#include "HistoryManager.h"

#include <QtTest>
#include <QTemporaryDir>

// Unit-тесты HistoryManager на временной SQLite-БД (без сети). Прогресс
// просмотра — единственное персистентное состояние, кроме настроек.
// Запуск: ctest --test-dir build -R history_manager
class TestHistoryManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void progressRoundtrip();
    void positionAndFlush();
    void clearRemoves();
    void mostRecentWorks();

private:
    QTemporaryDir m_dir;
};

void TestHistoryManager::initTestCase() {
    QVERIFY(m_dir.isValid());
    // AppConfig::resolveDataDirectory() читает env один раз (кэш) — ставим
    // ДО первого обращения к синглтонам, чтобы БД легла во временную папку.
    qputenv("ANIME_CLIENT_DATA_DIR", m_dir.path().toUtf8());
}

void TestHistoryManager::progressRoundtrip() {
    HistoryManager *hm = HistoryManager::instance();
    hm->setActive(QStringLiteral("title-1"), 5, QStringLiteral("kodik_123"));
    const QVariantMap p = hm->loadProgress(QStringLiteral("title-1"));
    QVERIFY(p.value(QStringLiteral("found")).toBool());
    QCOMPARE(p.value(QStringLiteral("episode")).toInt(), 5);
    QCOMPARE(p.value(QStringLiteral("translationId")).toString(), QStringLiteral("kodik_123"));
}

void TestHistoryManager::positionAndFlush() {
    HistoryManager *hm = HistoryManager::instance();
    hm->setActive(QStringLiteral("title-pos"), 2, QStringLiteral("cvh_9"));
    hm->reportPosition(1234.5);
    hm->flush();
    const QVariantMap p = hm->loadProgress(QStringLiteral("title-pos"));
    QVERIFY(p.value(QStringLiteral("found")).toBool());
    QCOMPARE(p.value(QStringLiteral("episode")).toInt(), 2);
    QCOMPARE(p.value(QStringLiteral("positionSeconds")).toDouble(), 1234.5);
    // Нет активного тайтла — reportPosition молча игнорируется
    hm->clear(QStringLiteral("title-pos"));
    hm->reportPosition(999.0);
    hm->flush();
    QVERIFY(!hm->loadProgress(QStringLiteral("title-pos")).value(QStringLiteral("found")).toBool());
}

void TestHistoryManager::clearRemoves() {
    HistoryManager *hm = HistoryManager::instance();
    hm->setActive(QStringLiteral("title-2"), 3, QString());
    QVERIFY(hm->loadProgress(QStringLiteral("title-2")).value(QStringLiteral("found")).toBool());
    hm->clear(QStringLiteral("title-2"));
    QVERIFY(!hm->loadProgress(QStringLiteral("title-2")).value(QStringLiteral("found")).toBool());
}

void TestHistoryManager::mostRecentWorks() {
    HistoryManager *hm = HistoryManager::instance();
    hm->setActive(QStringLiteral("title-recent"), 7, QString());
    const QVariantMap recent = hm->mostRecent();
    QVERIFY(recent.value(QStringLiteral("found")).toBool());
    QCOMPARE(recent.value(QStringLiteral("titleId")).toString(), QStringLiteral("title-recent"));
    QCOMPARE(recent.value(QStringLiteral("episode")).toInt(), 7);
}

QTEST_GUILESS_MAIN(TestHistoryManager)
#include "test_history_manager.moc"
