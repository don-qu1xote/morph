#include "ListenTracker.h"

void ListenTracker::trackStarted(const QString& service, const QString& trackId, const QString& albumId) {
    // новый трек при недоигранном старом значит скип
    finish(false);

    m_service = service;
    m_trackId = trackId;
    m_albumId = albumId;
    m_playedMs = 0;
    m_lengthMs = 0;

    if (m_playing) m_clock.start();
    else m_clock.invalidate();
}

void ListenTracker::playbackChanged(bool playing) {
    m_playing = playing;

    if (playing) {
        if (!m_clock.isValid()) m_clock.start();
        return;
    }

    if (m_clock.isValid()) {
        m_playedMs += m_clock.elapsed();
        m_clock.invalidate();
    }
}

void ListenTracker::durationChanged(qint64 durationMs) {
    // при смене источника прилетает 0 от старого трека, его пропускаем
    if (!m_trackId.isEmpty() && durationMs > 0) m_lengthMs = durationMs;
}

void ListenTracker::trackEnded() {
    finish(true);
}

void ListenTracker::finish(bool completed) {
    if (m_trackId.isEmpty()) return;

    if (m_clock.isValid()) {
        m_playedMs += m_clock.elapsed();
        m_clock.invalidate();
    }

    emit listenFinished(m_service, m_trackId, m_albumId, m_playedMs / 1000.0, m_lengthMs / 1000.0, completed);
    m_trackId.clear();
}
