#include "TasteVectors.h"
#include <cmath>

using Sum = std::array<double, MusicSpace::kDim>;

static MusicSpace::Vector toVector(const Sum& sum) {
    MusicSpace::Vector v;
    for (int d = 0; d < MusicSpace::kDim; ++d) v[d] = float(sum[d]);
    return MusicSpace::normalized(v);
}

static bool isZero(const MusicSpace::Vector& v) {
    for (float f : v) {
        if (f != 0) return false;
    }
    return true;
}

double TasteVectors::longWeight(const ListenEvent& event) {
    switch (event.kind) {
    case ListenEvent::Listen:
        if (event.finished()) return 1;
        return event.skipped() ? -1 : 0;
    case ListenEvent::Like:
        return 3;
    case ListenEvent::Unlike:
        return -1;
    case ListenEvent::Dislike:
        return -3;
    }
    return 0;
}

double TasteVectors::sessionWeight(const ListenEvent& event) {
    switch (event.kind) {
    case ListenEvent::Listen:
        if (event.finished()) return 1;
        if (event.skipped()) return -1;
        // дослушал половину: ни туда ни сюда, дальше по доле
        if (event.length > 0) return qBound(-1.0, (event.played / event.length - 0.5) * 2, 1.0);
        return 0;
    case ListenEvent::Like:
        return 1;
    case ListenEvent::Unlike:
        return 0;
    case ListenEvent::Dislike:
        return -1;
    }
    return 0;
}

void TasteVectors::rebuild(const QList<ListenEvent>& events, const QVariantList& likes, const VectorOf& vectorOf) {
    *this = TasteVectors();
    for (const QVariant& t : likes) {
        std::optional<Vector> v = vectorOf(t.toMap());
        if (!v) continue;
        for (int d = 0; d < MusicSpace::kDim; ++d) m_likes[d] += (*v)[d];
        m_hasLong = true;
    }
    for (const ListenEvent& e : events) add(e, vectorOf(e.track));
}

void TasteVectors::add(const ListenEvent& event, const std::optional<Vector>& v) {
    if (v && !isZero(*v)) addLong(event, *v);
    addSession(event, v ? *v : Vector{});
}

void TasteVectors::addLong(const ListenEvent& event, const Vector& v) {
    double r = longWeight(event);
    if (r == 0) return;
    // сумма затухает до момента нового события, потом к ней прибавляется событие
    if (m_longAt > 0 && event.at > m_longAt) {
        double decay = std::pow(0.5, double(event.at - m_longAt) / 86400.0 / kHalfLifeDays);
        for (double& value : m_long) value *= decay;
    }
    m_longAt = qMax(m_longAt, event.at);
    for (int d = 0; d < MusicSpace::kDim; ++d) m_long[d] += r * v[d];
    m_hasLong = true;
}

void TasteVectors::addSession(const ListenEvent& event, const Vector& v) {
    if (m_sessionAt > 0 && event.at - m_sessionAt > kSessionGap) {
        m_session = Vector{};
        m_sessionLength = 0;
    }
    m_sessionAt = qMax(m_sessionAt, event.at);
    if (event.kind == ListenEvent::Listen) ++m_sessionLength;
    double r = sessionWeight(event);
    if (r == 0 || isZero(v)) return;
    Vector next;
    for (int d = 0; d < MusicSpace::kDim; ++d) next[d] = float(0.7 * m_session[d] + 0.3 * r * v[d]);
    m_session = MusicSpace::normalized(next);
}

std::optional<TasteVectors::Vector> TasteVectors::longTerm(qint64 now) const {
    if (!m_hasLong) return std::nullopt;
    double decay = m_longAt > 0 && now > m_longAt ? std::pow(0.5, double(now - m_longAt) / 86400.0 / kHalfLifeDays) : 1;
    Sum sum;
    for (int d = 0; d < MusicSpace::kDim; ++d) sum[d] = m_long[d] * decay + m_likes[d];
    Vector v = toVector(sum);
    if (isZero(v)) return std::nullopt;
    return v;
}

std::optional<TasteVectors::Vector> TasteVectors::session(qint64 now) const {
    if (m_sessionAt == 0 || now - m_sessionAt > kSessionGap || isZero(m_session)) return std::nullopt;
    return m_session;
}

int TasteVectors::sessionLength(qint64 now) const {
    if (m_sessionAt == 0 || now - m_sessionAt > kSessionGap) return 0;
    return m_sessionLength;
}

std::optional<TasteVectors::Vector> TasteVectors::current(qint64 now) const {
    std::optional<Vector> l = longTerm(now);
    std::optional<Vector> s = session(now);
    if (!s) return l;
    if (!l) return s;
    double alpha = qMin(0.6, 0.1 * sessionLength(now));
    Vector mixed;
    for (int d = 0; d < MusicSpace::kDim; ++d) mixed[d] = float(alpha * (*s)[d] + (1 - alpha) * (*l)[d]);
    return MusicSpace::normalized(mixed);
}
