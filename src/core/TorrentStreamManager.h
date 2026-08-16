#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <functional>

// Надёжный менеджер стриминга торрентов через TorrServer.
//
// Главная проблема, которую он решает: ссылку на поток нельзя сразу отдавать
// в libmpv. Сразу после add_magnet TorrServer ещё не знает список файлов
// (стадия "getting info"), а после выбора файла буферу нужно время набрать
// первые куски — отсюда HTTP 404/обрывы и крэш плеера. Здесь это разбито на
// явные стадии с поллингом и сигналами вместо падения:
//   ensureServerRunning -> addMagnet -> waitForFiles -> waitForStreamReady -> streamReady
class TorrentStreamManager : public QObject {
    Q_OBJECT
public:
    explicit TorrentStreamManager(QObject *parent = nullptr);
    ~TorrentStreamManager() override;

    // Полный пайплайн: запустить сервер (если не запущен) -> добавить магнет
    // -> дождаться файлов -> выбрать серию -> дождаться готовности потока (200/206)
    // -> эмитить streamReady(url). На каждом шаге шлёт statusChanged с текстом
    // для QML вместо тихого зависания.
    Q_INVOKABLE void play(const QString &magnet, int episode, const QString &title);

    // Вызывается из QML когда пользователь выбрал файл вручную из списка torrentFilesReady.
    Q_INVOKABLE void playFile(const QString &hash, int fileId);

    Q_INVOKABLE void shutdownServer();

signals:
    void statusChanged(const QString &message);   // "Запуск TorrServer...", "Буферизация..."
    void buffering(bool isBuffering);
    void streamReady(const QString &url);
    void errorOccurred(const QString &message);
    // Серия не найдена однозначно — QML показывает список, пользователь выбирает файл вручную.
    void torrentFilesReady(const QVariantList &files); // [{name, id, sizeMb}]

private:
    void ensureServerRunning(std::function<void()> onReady, int session);
    void addMagnet(const QString &magnet, const QString &title, int session,
                   std::function<void(QString hash)> onAdded);
    void waitForFiles(const QString &hash, const QString &magnet, const QString &title, int session,
                      int readdAttempts, std::function<void(QJsonObject info)> onFiles);
    void pickAndWaitStream(const QString &hash, const QJsonObject &info, int episode, int session);

    bool isServerResponding();
    bool isCurrentSession(int session) const { return session == m_playSession; }
    QString host() const { return "http://127.0.0.1:8090"; }

    QProcess *m_serverProcess = nullptr;
    bool m_serverConfirmedRunning = false;
    int m_playSession = 0;
    QString m_lastHash;
};
