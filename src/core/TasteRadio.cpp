#include "TasteRadio.h"
#include "KnowledgeBase.h"
#include "MusicSpace.h"
#include "Recommender.h"
#include "TrackMatch.h"
#include "../services/YouTubeService.h"
#include <QSet>
#include <algorithm>

// по скольким последним сидам искать соседей за раз
static const int kSeedsPerRound = 3;
// у одного артиста не больше двух треков в одной порции
static const int kPerArtist = 2;

TasteRadio::TasteRadio(KnowledgeBase* knowledge, Recommender* recommender, QObject* parent)
    : QObject(parent), knowledge(knowledge), recommender(recommender) {}

bool TasteRadio::isAvailable() const {
    return knowledge->hasSource() || knowledge->size() > 0;
}

double TasteRadio::score(double match, double cosine, bool hasVector) {
    double closeness = hasVector ? (cosine + 1) / 2 : 0.5;
    return 0.5 * qBound(0.0, match, 1.0) + 0.5 * closeness;
}

void TasteRadio::candidates(const QList<QVariantMap>& seeds, int count, std::function<void(const QVariantList&)> done) {
    QList<QVariantMap> round = seeds.mid(0, kSeedsPerRound);
    if (round.isEmpty() || count <= 0 || !isAvailable()) {
        done({});
        return;
    }
    auto pending = std::make_shared<int>(int(round.size()));
    auto gathered = std::make_shared<QList<KnowledgeBase::Neighbor>>();
    QSet<QString> seedKeys;
    for (const QVariantMap& s : round) seedKeys.insert(TrackMatch::songKey(s));

    for (const QVariantMap& seed : round) {
        knowledge->similarTracks(seed, [this, pending, gathered, seedKeys, count, done](const auto& neighbors) {
            *gathered += neighbors;
            if (--*pending > 0) return;

            std::shared_ptr<const MusicSpace> space = m_space ? m_space() : nullptr;
            std::optional<MusicSpace::Vector> taste = recommender->taste().current(recommender->now());
            QHash<QString, Pick> best;
            for (const KnowledgeBase::Neighbor& n : std::as_const(*gathered)) {
                QVariantMap t{{"artist", n.artist}, {"title", n.title}};
                QString key = TrackMatch::songKey(t);
                if (key.isEmpty() || seedKeys.contains(key)) continue;
                std::optional<MusicSpace::Vector> v;
                if (space && taste) v = space->song(t, knowledge->knownSimilarTracks(t));
                double s = score(n.match, v ? MusicSpace::cosine(*taste, *v) : 0, v.has_value());
                // один сосед у нескольких сидов: это сильный сигнал, берём лучшую оценку и чуть сверху
                auto it = best.find(key);
                if (it != best.end()) it->score = qMax(it->score, s) + 0.05;
                else best.insert(key, {n.artist, n.title, n.durationMs, s});
            }
            QList<Pick> picks = best.values();
            std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
                return a.score != b.score ? a.score > b.score : a.title < b.title;
            });
            QHash<QString, int> perArtist;
            QVariantList diverse;
            for (const Pick& p : std::as_const(picks)) {
                int& n = perArtist[TrackMatch::artistKey(QVariantMap{{"artist", p.artist}})];
                if (n >= kPerArtist) continue;
                ++n;
                diverse.append(playable(p.artist, p.title, p.durationMs));
                if (diverse.size() >= count) break;
            }
            done(diverse);
        });
    }
}

QVariantMap TasteRadio::playable(const QString& artist, const QString& title, qint64 durationMs) {
    return {{"id", YouTubeService::queryTrackId(artist + " - " + title)},
            {"service", "YouTube"},
            {"artist", artist},
            {"title", title},
            {"durationMs", durationMs},
            {"via", "Last.fm"}};
}
