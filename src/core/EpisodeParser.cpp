#include "EpisodeParser.h"

#include <QRegularExpression>
#include <QVector>

bool EpisodeParser::looksLikeNoise(const QString &context, int value) {
    // Разрешения/частоты — частый источник ложных "серий"
    static const QVector<int> resolutions = {480, 576, 720, 1080, 1440, 2160};
    if (resolutions.contains(value)) {
        // "1080p"/"720p" рядом с числом — почти наверняка не серия
        QRegularExpression resHint(QString("%1\\s*p\\b").arg(value));
        if (resHint.match(context).hasMatch())
            return true;
    }
    // Годы 19xx/20xx — не серии
    if (value >= 1900 && value <= 2100)
        return true;
    return false;
}

int EpisodeParser::parse(const QString &fileName) {
    // Приоритет — от самых однозначных шаблонов к самым общим, чтобы
    // "Episode 05" не спутать с произвольным числом где-то в названии.
    static const QVector<QRegularExpression> patterns = {
        // "S01E05", "s1e05" — сезон+серия (частый формат TV/HD-раздач).
        // Раньше не распознавался: \b в e05-шаблоне не срабатывает, потому
        // что перед "E" идёт цифра сезона (границы слова нет).
        QRegularExpression(R"(\b[Ss]\d{1,2}[Ee]0*(\d{1,4})\b)"),
        // "Episode 05", "Эпизод 05", "Ep.05", "Ep 05"
        QRegularExpression(R"((?:episode|эпизод|серия|ep)[\s._-]*0*(\d{1,4})\b)",
                            QRegularExpression::CaseInsensitiveOption),
        // "e05", "E05" как отдельный токен (не часть слова/другого числа)
        QRegularExpression(R"(\be0*(\d{1,4})\b)", QRegularExpression::CaseInsensitiveOption),
        // "[05]", "(05)", "- 05 -" — номер в скобках/между разделителями
        QRegularExpression(R"([\[\(]\s*0*(\d{1,4})\s*[\]\)])"),
        QRegularExpression(R"(-\s*0*(\d{1,4})\s*-)"),
        // " - 05" на конце имени перед расширением, напр. "Title - 05.mkv"
        QRegularExpression(R"(-\s*0*(\d{1,4})\s*(?:\.[a-zA-Z0-9]+)?$)"),
        // "Title 05 [info].mkv" — число между пробелами/разделителями внутри имени.
        // Типично для аниме-раздач: "Kiss X Sis 01 [BDRip 1080p].mkv".
        // Ищем самое правое вхождение, чтобы не зацепить год или разрешение
        // (они уже отфильтрованы looksLikeNoise).
        QRegularExpression(R"((?:^|[\s._\-])0*(\d{1,4})(?=[\s._\[\(]))"),
        // голое число перед расширением: "05.mp4", "Title 05.mkv"
        QRegularExpression(R"((?:^|[\s._])0*(\d{1,4})\.[a-zA-Z0-9]{2,4}$)"),
    };

    for (const auto &re : patterns) {
        auto it = re.globalMatch(fileName);
        QRegularExpressionMatch lastMatch;
        while (it.hasNext()) {
            auto m = it.next();
            int value = m.captured(1).toInt();
            if (value <= 0 || value > 9999)
                continue;
            if (looksLikeNoise(fileName, value))
                continue;
            lastMatch = m;
        }
        if (lastMatch.hasMatch())
            return lastMatch.captured(1).toInt();
    }
    return -1;
}

int EpisodeParser::pickEpisodeIndex(const QStringList &files, int episode) {
    if (episode <= 0)
        return -1;

    QVector<int> parsed;
    parsed.reserve(files.size());
    bool anyParsed = false;
    for (const QString &f : files) {
        int ep = parse(f);
        parsed.push_back(ep);
        if (ep > 0)
            anyParsed = true;
    }

    if (anyParsed) {
        for (int i = 0; i < parsed.size(); ++i) {
            if (parsed[i] == episode)
                return i;
        }
    }

    // Падаем на порядковый индекс — это надёжнее, чем выдавать "не найдено",
    // если раздача без номеров в именах файлов (один эпизод = один файл).
    if (episode >= 1 && episode <= files.size())
        return episode - 1;

    return -1;
}
