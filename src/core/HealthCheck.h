#pragma once

#include <QObject>
#include <QVariantMap>

// Диагностика «почему не играет»: 4 независимые проверки (прокси, TorrServer,
// Kodik, JacRed). Всё асинхронно — никакого QEventLoop на GUI-потоке.
// Состояние: results[key] = {state: "checking"|"ok"|"fail", message}.
// НЕ путать с per-source статусом озвучек на карточке (DetailBridge.sourceStatus).
class HealthCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY resultsChanged)

public:
    explicit HealthCheck(QObject *parent = nullptr);
    static HealthCheck *instance();

    QVariantMap results() const { return m_results; }
    bool checking() const { return m_pending > 0; }

    // Запускает все проверки; каждая финиширует своим результатом.
    Q_INVOKABLE void checkAll();

signals:
    void resultsChanged();

private:
    void setResult(const QString &key, const QString &state, const QString &message);
    void checkTorrServer();
    void checkProxy();
    void checkKodik();
    void checkJacred();

    QVariantMap m_results;
    int m_pending = 0;
};
