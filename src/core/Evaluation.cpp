#include "Evaluation.h"
#include "TrackMatch.h"
#include <QRandomGenerator>
#include <QSet>
#include <algorithm>
#include <cmath>

Evaluation::Report Evaluation::run(const QList<ListenEvent>& events, const Setup& setup) {
    Report report;
    report.events = int(events.size());
    if (events.isEmpty()) return report;

    qint64 now = events.first().at;
    Recommender rec{QString()};
    rec.setClock([&now]() { return now; });
    rec.setSeed(setup.seed);
    if (setup.vectorOf) rec.setVectors(setup.vectorOf);
    if (setup.soundOf) rec.setSounds(setup.soundOf);

    const int split = qBound(0, int(std::ceil(events.size() * (1 - setup.testShare))), int(events.size()) - 1);
    report.testFrom = events[split].at;
    QRandomGenerator random(setup.seed);
    // песни, которые уже звучали: из них берутся соперники настоящего трека
    QList<QVariantMap> heard;
    QSet<QString> heardKeys;
    QList<QPair<double, bool>> scored;
    double hits = 0;
    double gain = 0;

    for (int i = 0; i < events.size(); ++i) {
        const ListenEvent& e = events[i];
        now = e.at;
        const bool listen = e.kind == ListenEvent::Listen;
        const QString key = TrackMatch::songKey(e.track);

        if (i >= split && listen && (e.finished() || e.skipped())) {
            const double chance = rec.chance(e.track);
            scored.append({chance, e.finished()});
            QString source = e.track["via"].toString();
            if (source.isEmpty()) source = e.track["service"].toString();
            QPair<int, int>& s = report.skips[source];
            if (e.skipped()) ++s.first;
            ++s.second;

            if (e.finished() && report.ranked < setup.maxRanked && heard.size() > setup.negatives) {
                // равные оценки делят место пополам: иначе одинаковые песни давали бы лишние попадания
                double above = 0;
                for (int drawn = 0; drawn < setup.negatives;) {
                    const QVariantMap& other = heard[int(random.bounded(quint32(heard.size())))];
                    if (TrackMatch::songKey(other) == key) continue;
                    double c = rec.chance(other);
                    if (c > chance) above += 1;
                    else if (c == chance) above += 0.5;
                    ++drawn;
                }
                double place = above + 1;
                if (place <= 10) {
                    hits += 1;
                    gain += 1 / std::log2(1 + place);
                }
                ++report.ranked;
            }
        }

        switch (e.kind) {
        case ListenEvent::Listen:
            rec.listened(e.track, e.played, e.length, e.completed);
            if (!heardKeys.contains(key)) {
                heardKeys.insert(key);
                heard.append(e.track);
            }
            break;
        case ListenEvent::Like:
            rec.liked(e.track, true);
            break;
        case ListenEvent::Unlike:
            rec.liked(e.track, false);
            break;
        case ListenEvent::Dislike:
            rec.disliked(e.track);
            break;
        }
    }

    report.scored = int(scored.size());
    report.auc = auc(scored);
    if (report.ranked > 0) {
        report.hitRate = hits / report.ranked;
        report.ndcg = gain / report.ranked;
    }
    return report;
}

double Evaluation::auc(QList<QPair<double, bool>> scored) {
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const int n = int(scored.size());
    double positiveRanks = 0;
    int positives = 0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && scored[j].first == scored[i].first) ++j;
        const double rank = (i + 1 + j) / 2.0;
        for (int k = i; k < j; ++k) {
            if (!scored[k].second) continue;
            positiveRanks += rank;
            ++positives;
        }
        i = j;
    }
    const int negatives = n - positives;
    if (positives == 0 || negatives == 0) return 0.5;
    return (positiveRanks - positives * (positives + 1) / 2.0) / (double(positives) * negatives);
}
