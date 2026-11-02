#ifndef LISTENING_HISTORY_H
#define LISTENING_HISTORY_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Одно событие истории: прослушивание, лайк, снятый лайк или дизлайк
struct ListenEvent {
    enum Kind { Listen, Like, Unlike, Dislike };
    Kind kind = Listen;
    qint64 at = 0; // секунды с эпохи
    QVariantMap track;
    double played = 0;
    double length = 0;
    bool completed = false;

    // дослушал или почти: последние секунды часто съедает переход на следующий
    bool finished() const;
    // ушёл в первые полминуты
    bool skipped() const;
};

// Своя история на все площадки: Яндекс отдаёт только свою и не целиком, а
// SoundCloud и YouTube вообще ничего не знают о прослушиваниях в morph.
// Каждое событие строкой JSON в файле, старые сверху отрезаются
class ListeningHistory {
public:
    explicit ListeningHistory(const QString& path);

    void append(const ListenEvent& event);
    const QList<ListenEvent>& events() const { return m_events; }

    struct SongStats {
        QVariantMap track;
        int plays = 0;
        int finishes = 0;
        int skips = 0;
        qint64 lastPlayed = 0;
    };
    // по TrackMatch::songKey, только прослушивания не раньше since
    QHash<QString, SongStats> songs(qint64 since = 0) const;
    // Недавно игравшие песни для главной, последняя первой и каждая один раз.
    // Скипнутое сюда не попадает: пролистал за пять секунд, значит не слушал
    QVariantList recent(int limit) const;

private:
    void load();
    void rewrite() const;
    static QJsonObject toJson(const ListenEvent& event);
    static ListenEvent fromJson(const QJsonObject& object);

    QString m_path;
    QList<ListenEvent> m_events;
};

#endif
