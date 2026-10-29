#include "TrackMatch.h"
#include <QRegularExpression>
#include <QStringList>

namespace {

const QRegularExpression& brackets() {
    static const QRegularExpression re("[\\(\\[]([^\\)\\]]*)[\\)\\]]");
    return re;
}

QString firstArtist(const QString& artist) {
    static const QRegularExpression guests(",|&| feat\\.? | ft\\.? | x ", QRegularExpression::CaseInsensitiveOption);
    return artist.section(guests, 0, 0);
}

// слова в нижнем регистре, ё как е: на YouTube её пишут через раз
QStringList words(const QString& text) {
    QStringList out;
    QString word;
    for (QChar c : text.toLower()) {
        if (c == u'ё') c = u'е';
        if (c.isLetterOrNumber()) {
            word += c;
        } else if (!word.isEmpty()) {
            out.append(word);
            word.clear();
        }
    }
    if (!word.isEmpty()) out.append(word);
    return out;
}

double share(const QStringList& needles, const QStringList& haystack) {
    if (needles.isEmpty()) return 0;
    int found = 0;
    for (const QString& w : needles) {
        if (haystack.contains(w)) ++found;
    }
    return double(found) / needles.size();
}

// На YouTube и SoundCloud артист часто в самом названии: «Artist - Song».
// Возвращает артиста и название без скобок, а содержимое скобок отдельно
struct Parts {
    QString artist;
    QString title;
    QString extras;
};

Parts parts(const QVariantMap& track) {
    Parts p{track["artist"].toString(), track["title"].toString(), {}};
    int dash = p.title.indexOf(" - ");
    if (dash > 0) {
        p.artist = p.title.left(dash);
        p.title = p.title.mid(dash + 3);
    }
    QRegularExpressionMatchIterator it = brackets().globalMatch(p.title);
    while (it.hasNext()) p.extras += " " + it.next().captured(1);
    p.title.remove(brackets());
    return p;
}

// Другие версии той же песни. Если такое слово есть в названии, которое ищем
// (ищут как раз лайв), штрафа нет
const QStringList& otherVersions() {
    static const QStringList list = {"live",     "concert", "cover",     "remix",   "karaoke", "instrumental",
                                     "slowed",   "sped",    "nightcore", "reverb",  "8d",      "minus",
                                     "acapella", "mashup",  "reaction",  "boosted", "минус",   "кавер",
                                     "караоке",  "ремикс",  "лайв",      "концерт", "remake"};
    return list;
}

} // namespace

QString TrackMatch::key(const QVariantMap& track) {
    Parts p = parts(track);
    return words(firstArtist(p.artist)).join("") + "|" + words(p.title).join("");
}

// Очки складываются из трёх частей: название (половина веса), артист и
// длительность. Длительность самая надёжная примета: лайвы, ремиксы и
// чужие песни с тем же названием почти всегда другой длины
double TrackMatch::score(const QVariantMap& wanted, const QVariantMap& candidate) {
    QString wantedTitle = wanted["title"].toString();
    QStringList wantedWords = words(QString(wantedTitle).remove(brackets()));
    if (wantedWords.isEmpty()) return 0;

    Parts c = parts(candidate);
    QStringList titleWords = words(c.title);
    // коэффициент Дайса: и пропущенные слова, и лишние снижают похожесть
    int common = 0;
    for (const QString& w : wantedWords) {
        if (titleWords.contains(w)) ++common;
    }
    // ни одного общего слова в названии: другая песня, как бы ни совпало остальное
    if (common == 0) return 0;
    double title = 2.0 * common / (wantedWords.size() + titleWords.size());

    // артиста ищем где угодно: в поле автора, в названии или в имени канала
    QStringList everywhere = words(candidate["artist"].toString() + " " + candidate["title"].toString());
    QStringList artistWords = words(firstArtist(wanted["artist"].toString()));
    double artist = artistWords.isEmpty() ? 0.5 : share(artistWords, everywhere);

    // разница до 3 секунд это тот же файл, к 30 секундам вклад падает до нуля,
    // а дальше уходит в минус: на минуту длиннее это уже другая запись
    double duration = 0.5;
    qint64 wantedMs = wanted["durationMs"].toLongLong();
    qint64 candidateMs = candidate["durationMs"].toLongLong();
    if (wantedMs > 0 && candidateMs > 0) {
        double diff = qAbs(wantedMs - candidateMs) / 1000.0;
        duration = diff <= 3 ? 1.0 : qMax(-1.0, 1.0 - (diff - 3) / 27);
    }

    QStringList candidateAll = words(c.title + " " + c.extras);
    QStringList wantedAll = words(wantedTitle);
    double penalty = 0;
    for (const QString& w : otherVersions()) {
        if (candidateAll.contains(w) && !wantedAll.contains(w)) penalty += 0.3;
    }

    return qBound(0.0, 0.5 * title + 0.2 * artist + 0.3 * duration - penalty, 1.0);
}

QVariantMap TrackMatch::fromQuery(const QString& query) {
    QString line = query.trimmed();
    int dash = line.indexOf(" - ");
    if (dash > 0) return {{"artist", line.left(dash)}, {"title", line.mid(dash + 3)}};
    return {{"artist", QString()}, {"title", line}};
}

QString TrackMatch::query(const QVariantMap& track) {
    QString artist = track["artist"].toString();
    QString title = track["title"].toString();
    return artist.isEmpty() ? title : artist + " - " + title;
}
