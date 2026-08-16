#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

// Скачивает постеры через NetworkManager (очередь, 2 параллельно) и отдаёт
// QML только file:// — иначе сотни Qt Quick Image грузят https в debug и падают.
class PosterCache : public QObject {
    Q_OBJECT
public:
    static PosterCache *instance();

    Q_INVOKABLE QString cachedFile(const QString &remoteUrl) const;
    Q_INVOKABLE QString posterId(const QString &remoteUrl) const;
    Q_INVOKABLE bool hasCached(const QString &remoteUrl) const;
    Q_INVOKABLE void request(const QString &remoteUrl);
    Q_INVOKABLE void requestPriority(const QString &remoteUrl);
    Q_INVOKABLE void preloadCatalog(const QVariantList &items);
    Q_INVOKABLE bool isRemotePoster(const QString &url) const;

    // Полная очистка дискового кэша картинок (постеры + http) и in-memory
    // карты готовых. Возвращает число удалённых файлов. Кнопка в Настройках.
    Q_INVOKABLE int clearDiskCache();

    QString filePathForId(const QString &id) const;

    // LRU: если кэш постеров > 500 МБ — удалить самые старые файлы.
    void enforceCacheLimit();

signals:
    void posterReady(const QString &remoteUrl, const QString &fileUrl);

private:
    explicit PosterCache(QObject *parent = nullptr);

    QString cacheDir() const;
    QString cachePathFor(const QString &remoteUrl) const;
    void pump();

    // Кэш результата валидации: полный декод JPEG (isValidImageFile) для одного
    // и того же файла раньше выполнялся при каждом cachedFile()/hasCached() —
    // на активации вкладки это 50 синхронных декодов в UI-потоке (~1.3 с).
    // Запоминаем валидные файлы по (path → size); при совпадении размера
    // считаем валидным без повторного декодирования. Только GUI-поток.
    bool validCached(const QString &path, const QString &remoteUrl) const;
    mutable QHash<QString, qint64> m_validatedSizes;

    mutable QHash<QString, QString> m_done;
    QSet<QString> m_inFlight;
    QStringList m_pendingHigh;
    QStringList m_pendingNormal;
    int m_active = 0;

    static constexpr int kMaxConcurrent = 4;
    static constexpr qint64 kMaxCacheSizeBytes = 500 * 1024 * 1024;

    void scheduleCacheLimitCheck();
    bool m_lruPending = false;
};