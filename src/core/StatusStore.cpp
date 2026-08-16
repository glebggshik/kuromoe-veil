#include "StatusStore.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>

#include "AppConfig.h"

StatusStore::StatusStore(QObject *parent) : QObject(parent) {
    openDatabase();
}

void StatusStore::closeDatabase() {
    if (m_db.isOpen())
        m_db.close();
}

StatusStore *StatusStore::instance() {
    static StatusStore *inst = new StatusStore(QCoreApplication::instance());
    return inst;
}

void StatusStore::openDatabase() {
    // Своё имя подключения — SQLite спокойно открывает один файл из нескольких
    // QSqlDatabase-подключений (как HistoryManager отдельно для watch_progress).
    m_db = QSqlDatabase::addDatabase("QSQLITE", "status_store_connection");
    m_db.setDatabaseName(AppConfig::historyDbPath());
    if (!m_db.open()) {
        qWarning("StatusStore: не удалось открыть БД: %s", qUtf8Printable(m_db.lastError().text()));
        return;
    }
    // WAL + busy_timeout — тот же файл держит HistoryManager отдельным
    // подключением, без этого чаще ловим SQLITE_BUSY на конкурентной записи.
    {
        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA journal_mode=WAL");
        pragma.exec("PRAGMA busy_timeout=5000");
        pragma.exec("PRAGMA synchronous=NORMAL");
    }
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS anime_status (
            shikimori_id TEXT PRIMARY KEY,
            title TEXT,
            poster TEXT,
            status TEXT NOT NULL,
            torrent_hash TEXT,
            torrent_magnet TEXT,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )");
    migrateSchema();
}

void StatusStore::migrateSchema() {
    // PRAGMA user_version — версия схемы БД (общая с HistoryManager, тот же
    // файл). Будущие изменения — блоками if (version < N), а не хаками в
    // CREATE TABLE IF NOT EXISTS (они не мигрируют существующие данные).
    int version = 0;
    {
        QSqlQuery ver(m_db);
        if (ver.exec("PRAGMA user_version") && ver.next())
            version = ver.value(0).toInt();
    }
    if (version < 1) {
        // v1 — начальные схемы watch_progress + anime_status (созданы выше).
        QSqlQuery setVer(m_db);
        setVer.exec("PRAGMA user_version = 1");
    }
}

void StatusStore::setStatus(const QString &titleId, const QString &status,
                             const QString &title, const QString &poster) {
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO anime_status (shikimori_id, title, poster, status, updated_at)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
        ON CONFLICT(shikimori_id) DO UPDATE SET
            status = excluded.status,
            title = CASE WHEN excluded.title != '' THEN excluded.title ELSE anime_status.title END,
            poster = CASE WHEN excluded.poster != '' THEN excluded.poster ELSE anime_status.poster END,
            updated_at = CURRENT_TIMESTAMP
    )");
    q.addBindValue(titleId);
    q.addBindValue(title);
    q.addBindValue(poster);
    q.addBindValue(status);
    q.exec();

    // просмотрено — больше не нужно держать раздачу скачанной/в TorrServer
    if (status == "watched") {
        QSqlQuery clear(m_db);
        clear.prepare("UPDATE anime_status SET torrent_hash = NULL, torrent_magnet = NULL WHERE shikimori_id = ?");
        clear.addBindValue(titleId);
        clear.exec();
    }
}

void StatusStore::removeStatus(const QString &titleId) {
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM anime_status WHERE shikimori_id = ?");
    q.addBindValue(titleId);
    q.exec();
}

QString StatusStore::currentStatus(const QString &titleId) {
    if (!m_db.isOpen())
        return QString();
    QSqlQuery q(m_db);
    q.prepare("SELECT status FROM anime_status WHERE shikimori_id = ?");
    q.addBindValue(titleId);
    if (!q.exec() || !q.next())
        return QString();
    return q.value(0).toString();
}

void StatusStore::setTorrent(const QString &titleId, const QString &hash, const QString &magnet,
                              const QString &title, const QString &poster) {
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO anime_status (shikimori_id, title, poster, status, torrent_hash, torrent_magnet)
        VALUES (?, ?, ?, 'watching', ?, ?)
        ON CONFLICT(shikimori_id) DO UPDATE SET
            torrent_hash = excluded.torrent_hash,
            torrent_magnet = excluded.torrent_magnet,
            updated_at = CURRENT_TIMESTAMP
    )");
    q.addBindValue(titleId);
    q.addBindValue(title);
    q.addBindValue(poster);
    q.addBindValue(hash);
    q.addBindValue(magnet);
    q.exec();
}

QVariantMap StatusStore::getTorrent(const QString &titleId) {
    QVariantMap result;
    result["hash"] = QString();
    result["magnet"] = QString();
    if (!m_db.isOpen())
        return result;
    QSqlQuery q(m_db);
    q.prepare("SELECT torrent_hash, torrent_magnet FROM anime_status WHERE shikimori_id = ?");
    q.addBindValue(titleId);
    if (!q.exec() || !q.next())
        return result;
    result["hash"] = q.value(0).toString();
    result["magnet"] = q.value(1).toString();
    return result;
}

QVariantList StatusStore::listByStatus(const QString &status) {
    QVariantList out;
    if (!m_db.isOpen())
        return out;
    QSqlQuery q(m_db);
    q.prepare("SELECT shikimori_id, title, poster FROM anime_status WHERE status = ? ORDER BY updated_at DESC");
    q.addBindValue(status);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap row;
        row["id"] = q.value(0).toString();
        row["title"] = q.value(1).toString();
        row["poster"] = q.value(2).toString();
        out << row;
    }
    return out;
}
