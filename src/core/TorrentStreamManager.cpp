#include "TorrentStreamManager.h"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>

#include "AppConfig.h"
#include "EpisodeParser.h"
#include "NetworkManager.h"
#include "StreamReadiness.h"

#include <QFileInfo>

namespace {
constexpr int kMaxAddRetries = 3;
// Раньше было 20с — для магнитов без встроенных метаданных (только
// трекеры/DHT, без webseed) реальный торрент-клиент внутри TorrServer
// может боотстрапить DHT и получать список файлов дольше 20с даже при
// формально высоком числе сидов у раздачи (см. лог TorrServer — bootstrap
// DHT сам по себе занял ~38с на одном из тестов). 20с обрубали ожидание
// раньше, чем успевали прийти метаданные.
constexpr int kFileWaitTimeoutMs = 45000;
constexpr int kStreamReadyMaxAttempts = 90; // 90 * 500ms = 45s максимум буферизации
}

TorrentStreamManager::TorrentStreamManager(QObject *parent) : QObject(parent) {}

QString TorrentStreamManager::host() const {
    AppConfig *cfg = AppConfig::instance();
    return QStringLiteral("http://%1:%2")
        .arg(cfg->torrServerHost())
        .arg(cfg->torrServerPort());
}

TorrentStreamManager::~TorrentStreamManager() {
    shutdownServer();
}

bool TorrentStreamManager::isServerResponding() {
    QNetworkRequest req(QUrl(host() + "/echo"));
    QNetworkReply *reply = NetworkManager::instance()->getLocal(req);
    QEventLoop loop;
    QTimer timeoutTimer;
    bool replyFinished = false;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, [&loop, &replyFinished]() {
        replyFinished = true;
        loop.quit();
    });
    timeoutTimer.start(1500);
    loop.exec();
    // Проверяем ОБА условия: reply должен завершиться (не просто таймаут)
    // И не иметь сетевой ошибки.
    // Без этого при молчаливом дропе SYN (Windows Firewall) таймер срабатывал
    // первым — reply->error() ещё NoError (ответ не получен, но ошибки нет),
    // и функция ложно возвращала true, пропуская запуск TorrServer.
    bool ok = replyFinished && reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

void TorrentStreamManager::ensureServerRunning(std::function<void()> onReady, int session) {
    if (m_serverConfirmedRunning && isServerResponding()) {
        if (isCurrentSession(session))
            onReady();
        return;
    }
    if (isServerResponding()) {
        m_serverConfirmedRunning = true;
        if (isCurrentSession(session))
            onReady();
        return;
    }

    QString exePath = AppConfig::instance()->torrServerPath();
    if (exePath.isEmpty()) {
        if (isCurrentSession(session))
            emit errorOccurred("TorrServer не найден. Укажи путь к TorrServer-windows-amd64.exe в настройках.");
        return;
    }

    if (isCurrentSession(session))
        emit statusChanged("Запуск TorrServer...");
    if (!m_serverProcess) {
        m_serverProcess = new QProcess(this);
        // Раньше вывод TorrServer уходил в nullDevice — если процесс сразу
        // же закрывался (например, порт уже занят другим TorrServer'ом),
        // причина оставалась невидимой, а плеер просто вис на "LOADING...".
        // Пробрасываем его stdout/stderr в наш лог.
        m_serverProcess->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_serverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            const QList<QByteArray> lines = m_serverProcess->readAllStandardOutput().split('\n');
            for (const QByteArray &line : lines) {
                const QByteArray trimmed = line.trimmed();
                if (!trimmed.isEmpty())
                    qInfo("TorrServer: %s", trimmed.constData());
            }
        });
        // Если TorrServer упал/закрылся — сбрасываем флаг, чтобы следующий
        // вызов ensureServerRunning не считал его живым и запустил заново.
        connect(m_serverProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
            qWarning("TorrServer: процесс завершился (exitCode=%d)", exitCode);
            m_serverConfirmedRunning = false;
        });
    }
    if (m_serverProcess->state() == QProcess::NotRunning) {
        qInfo("TorrServer: запускаем %s", qUtf8Printable(exePath));
        m_serverProcess->start(exePath, {});
    }

    auto *attempts = new int(0);
    auto *retryTimer = new QTimer(this);
    retryTimer->setInterval(200);
    connect(retryTimer, &QTimer::timeout, this, [this, onReady, attempts, retryTimer, session]() {
        if (!isCurrentSession(session)) {
            retryTimer->stop();
            retryTimer->deleteLater();
            delete attempts;
            return;
        }
        (*attempts)++;
        if (isServerResponding()) {
            m_serverConfirmedRunning = true;
            retryTimer->stop();
            retryTimer->deleteLater();
            delete attempts;
            onReady();
            return;
        }
        if (*attempts > 75) { // ~15s
            retryTimer->stop();
            retryTimer->deleteLater();
            delete attempts;
            emit errorOccurred("TorrServer не запустился за 15 секунд.");
        }
    });
    retryTimer->start();
}

void TorrentStreamManager::shutdownServer() {
    if (m_serverProcess && m_serverProcess->state() != QProcess::NotRunning) {
        NetworkManager::instance()->getLocal(QNetworkRequest{QUrl(host() + "/shutdown")});
        m_serverProcess->terminate();
        m_serverProcess->waitForFinished(2000);
    }
    m_serverConfirmedRunning = false;
}

void TorrentStreamManager::addMagnet(const QString &magnet, const QString &title, int session,
                                      std::function<void(QString)> onAdded) {
    auto *attempt = new int(0);
    auto tryAdd = std::make_shared<std::function<void()>>();
    *tryAdd = [this, magnet, title, onAdded, attempt, tryAdd, session]() {
        if (!isCurrentSession(session))
            return;

        QJsonObject body;
        body["action"] = "add";
        body["link"] = magnet;
        body["title"] = title;
        body["save_to_db"] = false;

        QNetworkRequest req(QUrl(host() + "/torrents"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(body).toJson());

        connect(reply, &QNetworkReply::finished, this, [this, reply, onAdded, attempt, tryAdd, session]() {
            if (!isCurrentSession(session)) {
                reply->deleteLater();
                return;
            }

            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QNetworkReply::NetworkError netErr = reply->error();
            const QString netErrText = reply->errorString();
            QByteArray data = reply->readAll();
            reply->deleteLater();

            // status==0 — соединение не дошло до HTTP-ответа вообще (connection
            // refused/reset). Раньше это сразу падало ошибкой без единой попытки
            // повтора и без лога — а TorrServer может секунду-другую не принимать
            // /torrents сразу после того как /echo уже ответил (внутренний bolt-db
            // ещё не готов). Ретраим как и 404.
            const bool retryableNetworkFailure = status == 0
                && netErr != QNetworkReply::NoError
                && *attempt < kMaxAddRetries - 1;
            if ((status == 404 || retryableNetworkFailure) && *attempt < kMaxAddRetries - 1) {
                (*attempt)++;
                qWarning("TorrServer: addMagnet попытка %d не удалась (HTTP %d, %s) — повтор",
                         *attempt, status, qUtf8Printable(netErrText));
                QTimer::singleShot(1000, this, [tryAdd, session, this]() {
                    if (isCurrentSession(session))
                        (*tryAdd)();
                });
                return;
            }
            if (status != 200) {
                qWarning("TorrServer: addMagnet провалился окончательно (HTTP %d, ошибка сети: %s)",
                         status, qUtf8Printable(netErrText));
                // Connection refused — сервер недоступен, сбрасываем флаг чтобы
                // следующая попытка play() перезапустила процесс, а не повисла
                // навечно считая что сервер жив.
                if (netErr != QNetworkReply::NoError)
                    m_serverConfirmedRunning = false;
                emit errorOccurred(QString("TorrServer: не удалось добавить раздачу (HTTP %1)").arg(status));
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(data).object();
            QString hash = obj["hash"].toString();
            if (hash.isEmpty()) {
                emit errorOccurred("TorrServer: ответ без hash раздачи");
                return;
            }
            onAdded(hash);
        });
    };
    (*tryAdd)();
}

void TorrentStreamManager::waitForFiles(const QString &hash, const QString &magnet,
                                        const QString &title, int session, int readdAttempts,
                                        std::function<void(QJsonObject)> onFiles) {
    if (isCurrentSession(session))
        emit statusChanged("Получение списка файлов раздачи...");
    auto deadline = std::make_shared<QDeadlineTimer>(kFileWaitTimeoutMs);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [this, hash, magnet, title, onFiles, deadline, poll, session, readdAttempts]() {
        if (!isCurrentSession(session))
            return;

        QJsonObject body;
        body["action"] = "get";
        body["hash"] = hash;
        QNetworkRequest req(QUrl(host() + "/torrents"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = NetworkManager::instance()->postLocal(req, QJsonDocument(body).toJson());

        connect(reply, &QNetworkReply::finished, this,
                [this, reply, hash, magnet, title, onFiles, deadline, poll, session, readdAttempts]() {
            if (!isCurrentSession(session)) {
                reply->deleteLater();
                return;
            }

            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray data = reply->readAll();
            reply->deleteLater();

            if (status == 404) {
                if (readdAttempts < 2) {
                    if (isCurrentSession(session))
                        emit statusChanged("Раздача сброшена, повторное добавление...");
                    addMagnet(magnet, title, session, [this, magnet, title, session, readdAttempts, onFiles](QString newHash) {
                        waitForFiles(newHash, magnet, title, session, readdAttempts + 1, onFiles);
                    });
                    return;
                }
                emit errorOccurred(
                    "TorrServer: раздача недоступна (404). Попробуй другой торрент или перезапусти TorrServer.");
                return;
            }
            QJsonObject info = QJsonDocument::fromJson(data).object();
            if (info.contains("file_stats") && info["file_stats"].toArray().size() > 0) {
                onFiles(info);
                return;
            }
            if (deadline->hasExpired()) {
                emit errorOccurred("Торрент не сообщил список файлов (timeout). Попробуй другую раздачу.");
                return;
            }
            QTimer::singleShot(500, this, [poll, session, this]() {
                if (isCurrentSession(session))
                    (*poll)();
            });
        });
    };
    (*poll)();
}

// Папки, типичные для бонус/доп. контента — OP/ED/NCOP/NCED/CM/Preview/Menu и т.п.
// Файлы внутри них при выборе серии идут в последнюю очередь.
static bool isBonusPath(const QString &path) {
    const QString lower = path.toLower();
    // Путь содержит бонус-сегмент как отдельный каталог (перед или после '/')
    static const QStringList kBonusDirs = {
        "bonus", "extra", "extras", "special", "specials",
        "ncop", "nced", "creditless", "cm", "commercial",
        "preview", "previews", "menu", "bd menu",
        "op", "ed", "opening", "ending",
    };
    for (const QString &seg : kBonusDirs) {
        if (lower.contains(QLatin1Char('/') + seg + QLatin1Char('/'))
            || lower.startsWith(seg + QLatin1Char('/'))
            || lower.contains(QLatin1Char('\\') + seg + QLatin1Char('\\')))
            return true;
    }
    // Имя файла (basename) — ищем OP/ED маркер как отдельный токен.
    // Примеры: "[Group] Title OP4 [info].mkv", "[Group] Title NCOP [info].mkv",
    //          "[Group] Title ED1 [info].mkv" — не начинаются с OP/ED, но содержат.
    const QString name = path.mid(qMax(path.lastIndexOf(QLatin1Char('/')),
                                       path.lastIndexOf(QLatin1Char('\\'))) + 1).toLower();
    static const QRegularExpression kBonusToken(
        QStringLiteral(R"((?:^|[\s_\[\(])(?:ncop|nced|creditless|op|ed|cm|pv|ova_ost)\s*\d*(?:[\s_\]\)\.\-]|$))"),
        QRegularExpression::CaseInsensitiveOption);
    if (kBonusToken.match(name).hasMatch())
        return true;
    return false;
}

void TorrentStreamManager::pickAndWaitStream(const QString &hash, const QJsonObject &info, int episode,
                                              int session) {
    if (!isCurrentSession(session))
        return;

    QJsonArray fileStats = info["file_stats"].toArray();

    struct FileEntry { QString path; int id; qint64 size; bool bonus; };
    QVector<FileEntry> all;
    for (const auto &v : fileStats) {
        QJsonObject f = v.toObject();
        QString path = f["path"].toString();
        const QString lo = path.toLower();
        if (!lo.endsWith(".mkv") && !lo.endsWith(".mp4") && !lo.endsWith(".avi"))
            continue;
        all.push_back({path, f["id"].toInt(), f["length"].toVariant().toLongLong(), isBonusPath(path)});
    }

    if (all.isEmpty()) {
        emit errorOccurred("В раздаче не найдено видеофайлов.");
        return;
    }

    // Предпочитаем основные (не-бонус) файлы. Если их нет — берём всё.
    QVector<FileEntry> main;
    for (const auto &e : all)
        if (!e.bonus)
            main.push_back(e);
    const QVector<FileEntry> &candidates = main.isEmpty() ? all : main;

    QStringList names;
    QVector<int> ids;
    for (const auto &e : candidates) {
        names << e.path;
        ids << e.id;
    }

    int idx = EpisodeParser::pickEpisodeIndex(names, episode);

    // Не нашли серию однозначно — показываем пользователю весь список файлов,
    // чтобы он мог выбрать вручную.
    if (idx < 0 || idx >= ids.size()) {
        qWarning("TorrentStreamManager: серия %d не найдена среди %d файлов, показываем список",
                 episode, candidates.size());
        QVariantList fileList;
        for (int i = 0; i < candidates.size(); ++i) {
            QVariantMap m;
            m["name"] = QFileInfo(candidates[i].path).fileName();
            m["path"] = candidates[i].path;
            m["id"]   = candidates[i].id;
            m["hash"] = hash;
            m["sizeMb"] = QString::number(candidates[i].size / 1024.0 / 1024.0, 'f', 0) + " МБ";
            fileList << m;
        }
        emit buffering(false);
        emit torrentFilesReady(fileList);
        return;
    }

    m_lastHash = hash;
    QString url = QString("%1/stream?link=%2&index=%3&play=true").arg(host(), hash).arg(ids[idx]);
    emit buffering(true);
    emit statusChanged("Буферизация потока...");

    StreamReadiness::waitUntilReady(
        url, StreamReadiness::Route::Local, QString(),
        [this, url, session](bool ok, int status) {
            if (!isCurrentSession(session))
                return;
            emit buffering(false);
            if (ok) {
                emit streamReady(url);
            } else {
                emit errorOccurred(
                    QString("Поток не стал доступен за отведённое время (последний статус: %1).").arg(status));
            }
        },
        kStreamReadyMaxAttempts);
}

void TorrentStreamManager::playFile(const QString &hash, int fileId) {
    ++m_playSession;
    const int session = m_playSession;
    m_lastHash = hash;
    QString url = QString("%1/stream?link=%2&index=%3&play=true").arg(host(), hash).arg(fileId);
    emit buffering(true);
    emit statusChanged("Буферизация потока...");
    StreamReadiness::waitUntilReady(
        url, StreamReadiness::Route::Local, QString(),
        [this, url, session](bool ok, int status) {
            if (!isCurrentSession(session)) return;
            emit buffering(false);
            if (ok)
                emit streamReady(url);
            else
                emit errorOccurred(
                    QString("Поток не стал доступен (статус: %1).").arg(status));
        }, kStreamReadyMaxAttempts);
}

void TorrentStreamManager::play(const QString &magnet, int episode, const QString &title) {
    // Новый запрос отменяет все колбэки предыдущего пайплайна (смена торрента/серии).
    ++m_playSession;
    const int session = m_playSession;

    ensureServerRunning([this, magnet, episode, title, session]() {
        if (!isCurrentSession(session))
            return;
        emit statusChanged("Добавление раздачи...");
        addMagnet(magnet, title, session, [this, magnet, title, episode, session](QString hash) {
            if (!isCurrentSession(session))
                return;
            waitForFiles(hash, magnet, title, session, 0, [this, hash, episode, session](QJsonObject info) {
                if (!isCurrentSession(session))
                    return;
                pickAndWaitStream(hash, info, episode, session);
            });
        });
    }, session);
}