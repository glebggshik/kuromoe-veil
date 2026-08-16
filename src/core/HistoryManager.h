#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QTimer>

// Прогресс просмотра — ТОЛЬКО SQLite (см. требование пользователя). Хранит
// для каждого titleId последнюю серию и точную позицию (сек.). При открытии
// тайтла бэкенд сам подтягивает запись и предлагает продолжить с неё.
class HistoryManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentEpisode READ currentEpisode NOTIFY currentChanged)
    Q_PROPERTY(double currentPosition READ currentPosition NOTIFY currentChanged)
    Q_PROPERTY(QString activeTitleId READ activeTitleId NOTIFY currentChanged)

public:
    explicit HistoryManager(QObject *parent = nullptr);
    ~HistoryManager() override;

    static HistoryManager *instance();

    struct Progress {
        bool found = false;
        int episode = 1;
        double positionSeconds = 0.0;
        QString translationId;
    };

    // Вызывается при открытии тайтла из любого источника (каталог, закладки,
    // поиск) — синхронный быстрый SELECT по индексированному PK.
    Q_INVOKABLE QVariantMap loadProgress(const QString &titleId);

    // Активный тайтл/серия — на него работает autosave-таймер ниже.
    Q_INVOKABLE void setActive(const QString &titleId, int episode, const QString &translationId);

    // Дёргается плеером на каждом обновлении позиции; реальная запись в БД
    // идёт не чаще раза в 5 секунд (см. m_saveTimer), а не на каждый кадр.
    Q_INVOKABLE void reportPosition(double seconds);

    Q_INVOKABLE void clear(const QString &titleId);

    // Последний обновлённый тайтл (по updated_at) — для hero-баннера
    // "продолжить просмотр" на главной. {found, titleId, episode}.
    Q_INVOKABLE QVariantMap mostRecent();

    int currentEpisode() const { return m_episode; }
    double currentPosition() const { return m_position; }
    QString activeTitleId() const { return m_activeTitleId; }

signals:
    void currentChanged(const QString &titleId, int episode);

public slots:
    // Публичный: вызывается из aboutToQuit до разборки QGuiApplication.
    void flush();
    // Явно закрыть SQLite (WAL checkpoint) до выхода — из Qwen workspace.
    void closeDatabase();

private:
    void openDatabase();
    void migrateSchema();

    QSqlDatabase m_db;
    QTimer m_saveTimer; // 5 секунд — см. требование автосохранения тайминга
    QString m_activeTitleId;
    int m_episode = 1;
    double m_position = 0.0;
    QString m_translationId;
    bool m_dirty = false;
};
