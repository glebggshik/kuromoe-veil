#pragma once

#include <QObject>
#include <QString>

// Вытаскивает номер серии из имени файла/метаданных раздачи. Заменяет
// единственный хрупкий regex Python-версии (только "[01]"), который не
// понимал "Episode 05", "e05", "05.mp4" и т.п. и давал "-1/26".
class EpisodeParser : public QObject {
    Q_OBJECT
public:
    explicit EpisodeParser(QObject *parent = nullptr) : QObject(parent) {}

    // Возвращает номер серии или -1, если не удалось распознать.
    Q_INVOKABLE static int parse(const QString &fileName);

    // pick: выбирает файл нужной серии из списка имён файлов раздачи.
    // Сначала пытается распознать номер по имени у каждого файла; если
    // распознать не удалось ни у одного (или раздача — "один файл = один
    // эпизод" без номеров в имени), падает обратно на порядковый индекс.
    // Возвращает индекс в files или -1.
    Q_INVOKABLE static int pickEpisodeIndex(const QStringList &files, int episode);

private:
    // true, если совпадение похоже на разрешение/битрейт/год, а не на номер
    // серии (1080, 720p, 2024 и т.п.) — такие совпадения нужно отбрасывать.
    static bool looksLikeNoise(const QString &context, int value);
};
