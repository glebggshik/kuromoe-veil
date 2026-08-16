#include "HistoryManager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

#include "AppConfig.h"

HistoryManager::HistoryManager(QObject *parent) : QObject(parent) {
    openDatabase();

    m_saveTimer.setInterval(5000); // автосохранение тайминга раз в 5 секунд
    connect(&m_saveTimer, &QTimer::timeout, this, &HistoryManager::flush);
    m_saveTimer.start();
}

HistoryManager::~HistoryManager() {
    flush();
    if (m_db.isOpen())
        m_db.close();
}

HistoryManager *HistoryManager::instance() {
    static HistoryManager inst;
    return &inst;
}

void HistoryManager::openDatabase() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "history_connection");
    m_db.setDatabaseName(AppConfig::historyDbPath());
    if (!m_db.open()) {
        qWarning("HistoryManager: не удалось открыть БД: %s",
                 qUtf8Printable(m_db.lastError().text()));
        return;
    }
    // WAL — второе соединение (StatusStore) пишет в тот же файл; без него
    // писатели блокируют друг друга сильнее и легче поймать SQLITE_BUSY.
    // busy_timeout — ждать лок вместо немедленного отказа при коллизии.
    {
        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA journal_mode=WAL");
        pragma.exec("PRAGMA busy_timeout=5000");
        pragma.exec("PRAGMA synchronous=NORMAL");
    }
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS watch_progress (
            title_id TEXT PRIMARY KEY,
            episode INTEGER NOT NULL DEFAULT 1,
            position_seconds REAL NOT NULL DEFAULT 0,
            translation_id TEXT,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )");
}

QVariantMap HistoryManager::loadProgress(const QString &titleId) {
    QVariantMap result;
    result["found"] = false;
    result["episode"] = 1;
    result["positionSeconds"] = 0.0;
    result["translationId"] = QString();

    if (!m_db.isOpen() || titleId.isEmpty())
        return result;

    QSqlQuery q(m_db);
    q.prepare("SELECT episode, position_seconds, translation_id FROM watch_progress WHERE title_id = ?");
    q.addBindValue(titleId);
    if (!q.exec() || !q.next())
        return result;

    result["found"] = true;
    result["episode"] = qMax(1, q.value(0).toInt());
    result["positionSeconds"] = q.value(1).toDouble();
    result["translationId"] = q.value(2).toString();
    return result;
}

void HistoryManager::setActive(const QString &titleId, int episode, const QString &translationId) {
    // переключение тайтла/серии — сразу пишем синхронно (это редкое событие,
    // в отличие от позиции, которая обновляется по таймеру), чтобы при
    // мгновенном крэше плеера серия не потерялась
    flush();
    m_activeTitleId = titleId;
    m_episode = episode;
    m_translationId = translationId;
    m_position = 0.0;
    m_dirty = true;
    flush();
    emit currentChanged(m_activeTitleId, m_episode);
}

void HistoryManager::reportPosition(double seconds) {
    if (m_activeTitleId.isEmpty())
        return;
    m_position = seconds;
    m_dirty = true;
}

void HistoryManager::flush() {
    if (!m_dirty || m_activeTitleId.isEmpty() || !m_db.isOpen())
        return;

    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO watch_progress (title_id, episode, position_seconds, translation_id, updated_at)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
        ON CONFLICT(title_id) DO UPDATE SET
            episode = excluded.episode,
            position_seconds = excluded.position_seconds,
            translation_id = excluded.translation_id,
            updated_at = CURRENT_TIMESTAMP
    )");
    q.addBindValue(m_activeTitleId);
    q.addBindValue(m_episode);
    q.addBindValue(m_position);
    q.addBindValue(m_translationId);
    if (!q.exec())
        qWarning("HistoryManager: ошибка сохранения прогресса: %s", qUtf8Printable(q.lastError().text()));
    m_dirty = false;
}

QVariantMap HistoryManager::mostRecent() {
    QVariantMap result;
    result["found"] = false;
    result["titleId"] = QString();
    result["episode"] = 1;

    if (!m_db.isOpen())
        return result;

    QSqlQuery q(m_db);
    q.exec("SELECT title_id, episode FROM watch_progress ORDER BY updated_at DESC LIMIT 1");
    if (!q.next())
        return result;

    result["found"] = true;
    result["titleId"] = q.value(0).toString();
    result["episode"] = q.value(1).toInt();
    return result;
}

void HistoryManager::clear(const QString &titleId) {
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM watch_progress WHERE title_id = ?");
    q.addBindValue(titleId);
    q.exec();
    if (titleId == m_activeTitleId) {
        m_activeTitleId.clear();
        m_dirty = false;
    }
}
