#include "Recommender.h"
#include <QDateTime>

Recommender::Recommender(const QString& dataDir, QObject* parent)
    : QObject(parent), m_now([]() { return QDateTime::currentSecsSinceEpoch(); }),
      m_history(dataDir + "/listening.jsonl") {
    for (const ListenEvent& e : m_history.events()) m_profile.add(e);
}

void Recommender::setLikes(const QVariantList& likes) {
    m_profile.setLikes(likes);
}

void Recommender::listened(const QVariantMap& track, double played, double length, bool completed) {
    ListenEvent e;
    e.track = track;
    e.played = played;
    // QML отдаёт длину плеера, она бывает 0 у потока, который не успел загрузиться
    e.length = length > 0 ? length : track["durationMs"].toLongLong() / 1000.0;
    e.completed = completed;
    record(e);
}

void Recommender::liked(const QVariantMap& track, bool liked) {
    ListenEvent e;
    e.kind = liked ? ListenEvent::Like : ListenEvent::Unlike;
    e.track = track;
    record(e);
}

void Recommender::disliked(const QVariantMap& track) {
    ListenEvent e;
    e.kind = ListenEvent::Dislike;
    e.track = track;
    record(e);
}

void Recommender::record(ListenEvent event) {
    if (event.track["id"].toString().isEmpty()) return;
    event.at = m_now();
    m_history.append(event);
    m_profile.add(event);
}
