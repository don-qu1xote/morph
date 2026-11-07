#include "Recommender.h"
#include "../utils/BackgroundWriter.h"
#include "TrackMatch.h"
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <cmath>

// подряд один артист не встаёт, а на десять треков не больше двух его
static const int kArtistWindow = 10;
static const int kArtistPerWindow = 2;
// из партии волны наружу уходит три четверти, но не меньше трёх треков
static const double kWaveShare = 0.75;
static const int kMinWave = 3;
// в запасе дольше двух партий: волна ушла дальше, они уже не к месту
static const int kWaveTurnsKept = 2;
static const int kSaveEvery = 5;

static QString daypart(qint64 at) {
    int hour = QDateTime::fromSecsSinceEpoch(at).time().hour();
    if (hour < 6) return "night";
    if (hour < 12) return "morning";
    if (hour < 18) return "day";
    return "evening";
}

Recommender::Recommender(const QString& dataDir, QObject* parent)
    : QObject(parent), m_now([]() { return QDateTime::currentSecsSinceEpoch(); }),
      m_history(dataDir + "/listening.jsonl"), m_rankerPath(dataDir + "/ranker.json") {
    for (const ListenEvent& e : m_history.events()) m_profile.add(e);
    m_songs = m_history.songs();

    BackgroundWriter::waitForAll();
    QFile file(m_rankerPath);
    if (file.open(QIODevice::ReadOnly)) m_ranker.load(file.readAll());
}

Recommender::~Recommender() {
    if (m_unsaved > 0) saveRanker();
}

void Recommender::setLikes(const QVariantList& likes) {
    m_profile.setLikes(likes);
    m_liked.clear();
    for (const QVariant& t : likes) m_liked.insert(TrackMatch::songKey(t.toMap()));
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

// признаки считаются до того, как событие попадёт в профиль: модель учится
// предсказывать по тому, что было известно в момент выбора трека
void Recommender::record(ListenEvent event) {
    if (event.track["id"].toString().isEmpty()) return;
    event.at = m_now();
    Ranker::Features x = features(event.track);

    m_history.append(event);
    m_profile.add(event);
    if (event.kind == ListenEvent::Listen) {
        ListeningHistory::SongStats& s = m_songs[TrackMatch::songKey(event.track)];
        s.track = event.track;
        ++s.plays;
        if (event.finished()) ++s.finishes;
        if (event.skipped()) ++s.skips;
        s.lastPlayed = event.at;
    }
    learn(event, x);
}

// Дослушал это 1, ушёл в первые полминуты 0, между ними доля прослушанного
// с половинным весом: может, трек и нравился, а выключил по другой причине
void Recommender::learn(const ListenEvent& event, const Ranker::Features& x) {
    double label = 0;
    double weight = 1;
    switch (event.kind) {
    case ListenEvent::Listen:
        if (event.finished()) label = 1;
        else if (!event.skipped() && event.length > 0) {
            label = qBound(0.0, event.played / event.length, 1.0);
            weight = 0.5;
        }
        break;
    case ListenEvent::Like:
        label = 1;
        break;
    case ListenEvent::Unlike:
        weight = 0.5;
        break;
    case ListenEvent::Dislike:
        weight = 2;
        break;
    }
    m_ranker.learn(x, label, weight);
    if (++m_unsaved >= kSaveEvery) saveRanker();
}

void Recommender::saveRanker() {
    BackgroundWriter::write(m_rankerPath, m_ranker.save());
    m_unsaved = 0;
}

bool Recommender::isExternal(const QVariantMap& track) {
    QString via = track["via"].toString();
    return via == "SoundCloud" || via == "YouTube";
}

bool Recommender::isNew(const QVariantMap& track) const {
    return !m_profile.known(TrackMatch::artistKey(track)) && !m_liked.contains(TrackMatch::songKey(track));
}

Ranker::Features Recommender::features(const QVariantMap& track) const {
    qint64 now = m_now();
    QString artist = TrackMatch::artistKey(track);
    QString song = TrackMatch::songKey(track);

    Ranker::Features x{{"bias", 1}, {"affinity", m_profile.affinity(artist, now)}};
    if (m_profile.known(artist)) x.append({"known", 1});
    if (m_profile.heard(artist) == 0) x.append({"new", 1});
    if (m_liked.contains(song)) x.append({"liked", 1});

    auto stats = m_songs.constFind(song);
    if (stats != m_songs.constEnd() && stats->plays > 0) {
        x.append({"plays", qMin(1.5, std::log1p(stats->plays) / std::log(20.0))});
        x.append({"skipRate", double(stats->skips) / stats->plays});
        // только что игравшая песня второй раз подряд надоедает
        x.append({"songRecent", std::exp(-(now - stats->lastPlayed) / 3600.0 / 24)});
    }
    qint64 lastArtist = m_profile.lastHeard(artist);
    if (lastArtist > 0) x.append({"artistRecent", std::exp(-(now - lastArtist) / 60.0 / 45)});

    QString service = track["service"].toString();
    if (!service.isEmpty()) x.append({"src:" + service, 1});
    // откуда совет и откуда звук это разное: похожее с SoundCloud может играть из Яндекса
    QString via = track["via"].toString();
    if (!via.isEmpty()) x.append({"via:" + via, 1});
    qint64 ms = track["durationMs"].toLongLong();
    if (ms > 0) x.append({"len", qBound(-1.5, (ms / 1000.0 - 200) / 120, 1.5)});
    // свой вес у каждого артиста и у артиста в это время суток: утром одно, ночью другое
    if (!artist.isEmpty()) {
        x.append({"a:" + artist, 1});
        x.append({"ad:" + artist + ":" + daypart(now), 1});
    }
    return x;
}

QVariantList Recommender::rank(const QVariantList& candidates, int count, bool explore, double newShare) {
    double share = newShare >= 0 ? newShare : m_newShare;
    struct Scored {
        QVariantMap track;
        QString artist;
        double p;
        bool fresh;
        bool external;
    };
    QList<Scored> pool;
    for (const QVariant& v : candidates) {
        QVariantMap t = v.toMap();
        pool.append({t, TrackMatch::artistKey(t), m_ranker.predict(features(t), explore), isNew(t), isExternal(t)});
    }
    std::stable_sort(pool.begin(), pool.end(), [](const Scored& a, const Scored& b) { return a.p > b.p; });

    QVariantList out;
    QStringList recent;
    int fresh = 0;
    int external = 0;
    auto fits = [&recent](const Scored& s) {
        if (!recent.isEmpty() && recent.last() == s.artist) return false;
        return recent.mid(qMax(0, recent.size() - kArtistWindow + 1)).count(s.artist) < kArtistPerWindow;
    };

    while (out.size() < count && !pool.isEmpty()) {
        // сколько незнакомых должно быть к этому месту: 0.3 даёт по три на десять.
        // Доля SoundCloud и YouTube это нижняя граница: больше можно, меньше нет
        bool wantFresh = fresh < int(std::floor(share * (out.size() + 1)));
        bool wantExternal = external < int(std::floor(m_externalShare * (out.size() + 1)));
        auto suits = [&](const Scored& s, int pass) {
            bool sourceOk = !wantExternal || s.external;
            switch (pass) {
            case 0:
                return sourceOk && s.fresh == wantFresh && fits(s);
            case 1:
                return sourceOk && fits(s);
            case 2:
                return s.fresh == wantFresh && fits(s);
            case 3:
                return fits(s);
            default:
                return true;
            }
        };
        int pick = -1;
        // по порядку: обе доли и разнообразие, доля площадок, доля нового, разнообразие, что есть
        for (int pass = 0; pass < 5 && pick < 0; ++pass) {
            for (int i = 0; i < pool.size(); ++i) {
                if (!suits(pool[i], pass)) continue;
                pick = i;
                break;
            }
        }
        Scored s = pool.takeAt(pick);
        if (s.fresh) ++fresh;
        if (s.external) ++external;
        recent.append(s.artist);
        out.append(s.track);
    }
    return out;
}

QVariantList Recommender::nextWave(const QVariantList& batch) {
    ++m_waveTurn;
    for (const QVariant& v : batch) {
        QVariantMap t = v.toMap();
        QString song = TrackMatch::songKey(t);
        bool waiting = std::any_of(m_wavePool.cbegin(), m_wavePool.cend(),
                                   [&song](const Pending& p) { return TrackMatch::songKey(p.track) == song; });
        if (!waiting && !m_waveGiven.contains(song)) m_wavePool.append({t, m_waveTurn});
    }
    m_wavePool.removeIf([this](const Pending& p) { return p.turn < m_waveTurn - kWaveTurnsKept; });

    int count = qMin(int(m_wavePool.size()), qMax(kMinWave, int(std::ceil(batch.size() * kWaveShare))));
    QVariantList candidates;
    for (const Pending& p : std::as_const(m_wavePool)) candidates.append(p.track);
    QVariantList picked = rank(candidates, count);

    QSet<QString> pickedIds;
    for (const QVariant& t : std::as_const(picked)) {
        pickedIds.insert(t.toMap()["id"].toString());
        m_waveGiven.insert(TrackMatch::songKey(t.toMap()));
    }
    m_wavePool.removeIf([&pickedIds](const Pending& p) { return pickedIds.contains(p.track["id"].toString()); });
    return picked;
}

void Recommender::resetWave() {
    m_wavePool.clear();
    m_waveGiven.clear();
    m_waveTurn = 0;
}
