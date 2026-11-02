#include "ListeningHistory.h"
#include "TrackMatch.h"
#include <QFile>
#include <QJsonDocument>
#include <QSet>

// десять тысяч событий это пара лет прослушиваний и пара мегабайт на диске
static const int kKeep = 10000;
// переписывать файл на каждом событии незачем, отрезаем пачкой
static const int kSlack = 2000;
static const double kSkipSeconds = 30;
static const double kFinishedShare = 0.8;

static const QStringList kTrackFields = {"id", "service", "artist", "title", "durationMs", "coverUrl", "album"};
static const QStringList kKinds = {"listen", "like", "unlike", "dislike"};

bool ListenEvent::finished() const {
    return kind == Listen && (completed || (length > 0 && played >= kFinishedShare * length));
}

bool ListenEvent::skipped() const {
    return kind == Listen && !finished() && played < kSkipSeconds;
}

ListeningHistory::ListeningHistory(const QString& path) : m_path(path) {
    load();
}

void ListeningHistory::append(const ListenEvent& event) {
    m_events.append(event);
    if (m_events.size() > kKeep + kSlack) {
        m_events = m_events.mid(m_events.size() - kKeep);
        rewrite();
        return;
    }

    QFile file(m_path);
    if (file.open(QIODevice::Append)) file.write(QJsonDocument(toJson(event)).toJson(QJsonDocument::Compact) + "\n");
}

QHash<QString, ListeningHistory::SongStats> ListeningHistory::songs(qint64 since) const {
    QHash<QString, SongStats> out;
    for (const ListenEvent& e : m_events) {
        if (e.kind != ListenEvent::Listen || e.at < since) continue;
        SongStats& s = out[TrackMatch::songKey(e.track)];
        s.track = e.track;
        ++s.plays;
        if (e.finished()) ++s.finishes;
        if (e.skipped()) ++s.skips;
        s.lastPlayed = qMax(s.lastPlayed, e.at);
    }
    return out;
}

void ListeningHistory::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return;
    while (!file.atEnd()) {
        QJsonObject object = QJsonDocument::fromJson(file.readLine()).object();
        // строка, недописанная при выключении, просто пропускается
        if (!object.isEmpty()) m_events.append(fromJson(object));
    }
    if (m_events.size() > kKeep) {
        m_events = m_events.mid(m_events.size() - kKeep);
        rewrite();
    }
}

QVariantList ListeningHistory::recent(int limit) const {
    QVariantList out;
    QSet<QString> seen;
    for (auto it = m_events.crbegin(); it != m_events.crend() && out.size() < limit; ++it) {
        if (it->kind != ListenEvent::Listen || it->skipped()) continue;
        QString key = TrackMatch::songKey(it->track);
        if (seen.contains(key)) continue;
        seen.insert(key);
        QVariantMap track = it->track;
        track["playedAt"] = it->at;
        out.append(track);
    }
    return out;
}

void ListeningHistory::rewrite() const {
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) return;
    for (const ListenEvent& e : m_events) file.write(QJsonDocument(toJson(e)).toJson(QJsonDocument::Compact) + "\n");
}

QJsonObject ListeningHistory::toJson(const ListenEvent& event) {
    QVariantMap track;
    for (const QString& f : kTrackFields) {
        if (event.track.contains(f)) track[f] = event.track[f];
    }
    QJsonObject o{{"k", kKinds.value(event.kind)}, {"at", event.at}, {"t", QJsonObject::fromVariantMap(track)}};
    if (event.kind == ListenEvent::Listen) {
        o["p"] = qRound(event.played * 10) / 10.0;
        o["l"] = qRound(event.length * 10) / 10.0;
        if (event.completed) o["c"] = true;
    }
    return o;
}

ListenEvent ListeningHistory::fromJson(const QJsonObject& object) {
    ListenEvent e;
    e.kind = ListenEvent::Kind(qMax(0, kKinds.indexOf(object["k"].toString())));
    e.at = object["at"].toInteger();
    e.track = object["t"].toObject().toVariantMap();
    e.played = object["p"].toDouble();
    e.length = object["l"].toDouble();
    e.completed = object["c"].toBool();
    return e;
}
