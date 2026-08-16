#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>

// Порт core/status_store.py — статусы просмотра (смотрю/просмотрено/брошено/
// в планах/отложено) + последняя использованная раздача для тайтла.
// Отдельная таблица (anime_status) от HistoryManager::watch_progress — эпизод
// и точная позиция остаются единственной обязанностью HistoryManager, здесь
// только статус и кэш magnet/hash, как в оригинале.
class StatusStore : public QObject {
    Q_OBJECT
public:
    explicit StatusStore(QObject *parent = nullptr);

    static StatusStore *instance();

    Q_INVOKABLE void setStatus(const QString &titleId, const QString &status,
                                const QString &title = QString(), const QString &poster = QString());
    Q_INVOKABLE void removeStatus(const QString &titleId);
    Q_INVOKABLE QString currentStatus(const QString &titleId);

    Q_INVOKABLE void setTorrent(const QString &titleId, const QString &hash, const QString &magnet,
                                 const QString &title = QString(), const QString &poster = QString());
    // {hash, magnet} — пусто, если для тайтла раздача ещё не выбиралась.
    Q_INVOKABLE QVariantMap getTorrent(const QString &titleId);

    Q_INVOKABLE QVariantList listByStatus(const QString &status);

    void closeDatabase();

private:
    void openDatabase();
    void migrateSchema();

    QSqlDatabase m_db;
};
