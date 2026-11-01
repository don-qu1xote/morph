#include "TasteProfile.h"
#include "TrackMatch.h"
#include <QtMath>
#include <algorithm>

static const double kHalfLifeSecs = 30.0 * 24 * 3600;
static const double kFinished = 1.0;
static const double kPartial = 0.3;
static const double kSkipped = -1.0;
static const double kLike = 2.0;
static const double kUnlike = -1.0;
static const double kDislike = -4.0;
// десять лайков дают около 3.6, примерно как месяц слушать каждый день
static const double kLikesWeight = 1.5;
static const double kScale = 4.0;

void TasteProfile::setLikes(const QVariantList& likes) {
    for (Artist& a : m_artists) a.likes = 0;
    for (const QVariant& v : likes) {
        QVariantMap track = v.toMap();
        QString key = TrackMatch::artistKey(track);
        if (key.isEmpty()) continue;
        Artist& a = m_artists[key];
        if (a.name.isEmpty()) a.name = TrackMatch::mainArtist(track);
        ++a.likes;
    }
}

void TasteProfile::add(const ListenEvent& event) {
    QString key = TrackMatch::artistKey(event.track);
    if (key.isEmpty()) return;
    Artist& a = m_artists[key];
    if (a.name.isEmpty()) a.name = TrackMatch::mainArtist(event.track);

    double weight = 0;
    switch (event.kind) {
    case ListenEvent::Listen:
        ++a.heard;
        a.lastHeard = qMax(a.lastHeard, event.at);
        if (event.finished()) ++a.finished;
        weight = event.finished() ? kFinished : (event.skipped() ? kSkipped : kPartial);
        break;
    case ListenEvent::Like:
        weight = kLike;
        break;
    case ListenEvent::Unlike:
        weight = kUnlike;
        break;
    case ListenEvent::Dislike:
        weight = kDislike;
        break;
    }

    // события приходят по порядку, поэтому сумму хватает пересчитать на новый момент
    a.recent = decayed(a, event.at) + weight;
    a.recentAt = qMax(a.recentAt, event.at);
}

double TasteProfile::affinity(const QString& artistKey, qint64 now) const {
    auto it = m_artists.constFind(artistKey);
    return it == m_artists.constEnd() ? 0 : std::tanh(raw(*it, now) / kScale);
}

double TasteProfile::affinityOf(const QVariantMap& track, qint64 now) const {
    return affinity(TrackMatch::artistKey(track), now);
}

int TasteProfile::heard(const QString& artistKey) const {
    return m_artists.value(artistKey).heard;
}

bool TasteProfile::known(const QString& artistKey) const {
    auto it = m_artists.constFind(artistKey);
    return it != m_artists.constEnd() && (it->finished > 0 || it->likes > 0);
}

qint64 TasteProfile::lastHeard(const QString& artistKey) const {
    return m_artists.value(artistKey).lastHeard;
}

QList<QPair<QString, double>> TasteProfile::top(int count, qint64 now) const {
    QList<QPair<QString, double>> out;
    for (auto it = m_artists.cbegin(); it != m_artists.cend(); ++it) {
        double score = raw(*it, now);
        if (score > 0) out.append({it.key(), std::tanh(score / kScale)});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > count) out.resize(count);
    return out;
}

double TasteProfile::decayed(const Artist& a, qint64 now) {
    if (a.recentAt == 0) return a.recent;
    double age = qMax<qint64>(0, now - a.recentAt);
    return a.recent * std::exp2(-age / kHalfLifeSecs);
}

double TasteProfile::raw(const Artist& a, qint64 now) const {
    return decayed(a, now) + kLikesWeight * std::log1p(a.likes);
}
