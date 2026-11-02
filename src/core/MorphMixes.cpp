#include "MorphMixes.h"
#include "CandidateFilter.h"
#include "Library.h"
#include "Recommender.h"
#include "TrackMatch.h"
#include "../services/SoundCloudService.h"
#include "../services/YandexService.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <memory>

static const qint64 kDay = 24 * 3600;
static const int kMixSize = 50;
static const int kListSize = 30;
static const int kMaxGroups = 4;
// группы ищутся среди стольких любимых артистов
static const int kGroupArtists = 40;
// перерыв дольше получаса начинает новую сессию
static const qint64 kSessionGap = 30 * 60;
// лайки, поставленные рядом, тоже говорят о близости, но слабее сессии
static const int kLikeWindow = 8;
static const double kLikeWeight = 0.5;
// за свою метку артист держится, пока у чужой нет явного перевеса
static const double kInertia = 0.25;
static const int kReleaseDays = 45;
static const int kReleaseArtists = 12;
static const int kReleaseAlbums = 8;
// доля нового в Daily Mix чуть выше общей: микс про знакомых, но с открытиями
static const double kMixNewShare = 0.35;
// артиста, которого ты пропускаешь, в миксы не зовём
static const double kDislikedArtist = -0.3;
// сколько треков плейлиста берётся в образцы для подбора
static const int kSuggestSeeds = 3;
// из скольких найденных по названию плейлистов брать треки
static const int kTitlePlaylists = 2;

// одно и то же значение на любой машине и в любой версии Qt (FNV-1a)
static quint32 stableHash(const QString& text, int salt) {
    quint32 h = 2166136261u ^ quint32(salt);
    for (char c : text.toUtf8()) {
        h ^= quint8(c);
        h *= 16777619u;
    }
    return h;
}

MorphMixes::MorphMixes(Recommender* recommender, Library* library, YandexService* yandex, SoundCloudService* soundcloud,
                       const QString& dataDir, QObject* parent)
    : QObject(parent), rec(recommender), library(library), yandex(yandex), soundcloud(soundcloud),
      m_discoverPath(dataDir + "/discover.json") {}

QVariantList MorphMixes::cards() {
    QVariantList out;

    const QList<QStringList> groups = artistGroups();
    const QVariantList known = familiar();
    for (int i = 0; i < groups.size(); ++i) {
        QStringList names;
        for (const QString& key : groups[i].mid(0, 3)) names.append(rec->profile().name(key));
        QVariantList own;
        for (const QVariant& t : known) {
            if (TrackMatch::artistKey(t.toMap()) == groups[i].first()) own.append(t);
        }
        out.append(
            card(QString("morph:daily%1").arg(i + 1), QString("Daily Mix %1").arg(i + 1), names.join(", "), own));
    }

    QVariantList seedList = seeds(8);
    if (seedList.size() >= 3)
        out.append(card("morph:discover", "Discover Weekly", "New artists close to your taste", seedList));
    if (artistIds().size() >= 3) {
        out.append(card("morph:releases", "Release Radar", "Fresh releases from your artists", seedList));
    }

    QVariantList repeat = onRepeat();
    if (repeat.size() >= 5) out.append(card("morph:onrepeat", "On Repeat", "Your most played this month", repeat));
    QVariantList old = rediscover();
    if (old.size() >= 10)
        out.append(card("morph:rediscover", "Rediscover", "Likes you haven't played in a while", old));
    return out;
}

void MorphMixes::build(const QString& id) {
    QString name = id.section(':', 1);
    if (name == "onrepeat") emit ready("On Repeat", "", onRepeat());
    else if (name == "rediscover") emit ready("Rediscover", "", rediscover());
    else if (name == "discover") buildDiscover();
    else if (name == "releases") buildReleaseRadar();
    else if (name.startsWith("daily")) buildDailyMix(name.mid(5).toInt() - 1);
    else emit failed("Unknown morph playlist: " + id);
}

void MorphMixes::startSeeded(const QVariantList& tracks) {
    ++m_seedGeneration;
    m_seeds = tracks;
    // каждый запуск начинает с другого места плейлиста
    std::shuffle(m_seeds.begin(), m_seeds.end(), *QRandomGenerator::global());
    m_seedPos = 0;
    m_seedBusy = false;
    nextSeeded();
}

void MorphMixes::nextSeeded() {
    if (m_seedBusy || m_seeds.isEmpty()) return;
    m_seedBusy = true;

    QVariantList seedList;
    QVariantList own;
    for (int i = 0; i < 3; ++i) seedList.append(m_seeds[(m_seedPos + i) % m_seeds.size()]);
    for (int i = 3; i < 5; ++i) own.append(m_seeds[(m_seedPos + i) % m_seeds.size()]);
    m_seedPos = (m_seedPos + 5) % m_seeds.size();

    int generation = m_seedGeneration;
    similarTo(seedList, 1, [this, own, generation](const QVariantList& similar) {
        if (generation != m_seedGeneration) return;
        m_seedBusy = false;
        emit seededBatch(own + similar);
    });
}

// Песни, которые ты знаешь: MAIN и дослушанное из истории, по одной на песню
QVariantList MorphMixes::familiar() const {
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant& v : library->tracks()) {
        QString key = TrackMatch::songKey(v.toMap());
        if (seen.contains(key)) continue;
        seen.insert(key);
        out.append(v);
    }
    const auto songs = rec->history().songs();
    for (auto it = songs.cbegin(); it != songs.cend(); ++it) {
        if (it->finishes == 0 || seen.contains(it.key())) continue;
        seen.insert(it.key());
        out.append(it->track);
    }
    return out;
}

// Дослушанное за месяц: дослушал плюс один, недослушал плюс 0.3, скип минус 0.5
QVariantList MorphMixes::onRepeat() const {
    QList<QPair<double, QVariantMap>> scored;
    const auto songs = rec->history().songs(rec->now() - 30 * kDay);
    for (const auto& s : songs) {
        double partial = s.plays - s.finishes - s.skips;
        double score = s.finishes + 0.3 * partial - 0.5 * s.skips;
        if (s.plays >= 2 && score > 0.5) scored.append({score, s.track});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    QVariantList out;
    for (const auto& [score, track] : std::as_const(scored)) {
        if (out.size() >= kListSize) break;
        out.append(track);
    }
    return out;
}

// Лайки, которых не было слышно два месяца. Свежие и так на слуху, поэтому
// при большой коллекции первые двадцать пропускаются. Порядок меняется раз
// в неделю, а внутри недели держится
QVariantList MorphMixes::rediscover() const {
    const QVariantList main = library->tracks();
    const auto recent = rec->history().songs(rec->now() - 60 * kDay);
    int from = main.size() > 60 ? 20 : 0;
    int salt = week();

    QList<QPair<quint32, QVariantMap>> shuffled;
    for (int i = from; i < main.size(); ++i) {
        QVariantMap t = main[i].toMap();
        QString key = TrackMatch::songKey(t);
        if (!recent.contains(key)) shuffled.append({stableHash(key, salt), t});
    }
    std::sort(shuffled.begin(), shuffled.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QVariantList out;
    for (const auto& [order, track] : std::as_const(shuffled)) {
        if (out.size() >= kListSize) break;
        out.append(track);
    }
    return out;
}

// Любимое сейчас, по одной песне на артиста: больше всего дослушанного за
// два месяца, потом свежие лайки. Нужен id в Яндексе, по нему ищется похожее
QVariantList MorphMixes::seeds(int count) const {
    QList<ListeningHistory::SongStats> top = rec->history().songs(rec->now() - 60 * kDay).values();
    std::stable_sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.finishes > b.finishes; });

    QVariantList out;
    QSet<QString> artists;
    auto take = [&](const QVariantMap& t) {
        QString artist = TrackMatch::artistKey(t);
        if (out.size() >= count || artists.contains(artist) || yandexIdOf(t).isEmpty()) return;
        artists.insert(artist);
        out.append(t);
    };
    for (const auto& s : std::as_const(top)) {
        if (s.finishes > 0) take(s.track);
    }
    for (const QVariant& t : library->tracks()) take(t.toMap());
    return out;
}

QString MorphMixes::yandexIdOf(const QVariantMap& track) const {
    if (track["service"].toString() == "Yandex") return track["id"].toString();
    return library->yandexId(TrackMatch::songKey(track));
}

// id любимых артистов в Яндексе, из их треков
QHash<QString, QString> MorphMixes::artistIds() const {
    QHash<QString, QString> ids;
    const auto top = rec->profile().top(kGroupArtists, rec->now());
    QSet<QString> wanted;
    for (const auto& [key, affinity] : top) wanted.insert(key);
    for (const QVariant& v : familiar()) {
        QVariantMap t = v.toMap();
        QString key = TrackMatch::artistKey(t);
        QString id = t["artistId"].toString();
        if (!id.isEmpty() && wanted.contains(key)) ids.insert(key, id);
    }
    return ids;
}

QList<QStringList> MorphMixes::artistGroups() const {
    const auto top = rec->profile().top(kGroupArtists, rec->now());
    QStringList artists;
    QHash<QString, double> affinity;
    for (const auto& [key, a] : top) {
        artists.append(key);
        affinity.insert(key, a);
    }
    if (artists.size() < 4) return {};

    // вес связи делится на размер группы: целый день музыки не делает всех соседями
    QHash<QString, QHash<QString, double>> link;
    auto together = [&link](const QStringList& group, double weight) {
        if (group.size() < 2) return;
        double w = weight / (group.size() - 1);
        for (int i = 0; i < group.size(); ++i) {
            for (int j = i + 1; j < group.size(); ++j) {
                link[group[i]][group[j]] += w;
                link[group[j]][group[i]] += w;
            }
        }
    };
    auto add = [&affinity](QStringList& group, const QVariantMap& track) {
        QString key = TrackMatch::artistKey(track);
        if (affinity.contains(key) && !group.contains(key)) group.append(key);
    };

    QStringList session;
    qint64 last = 0;
    for (const ListenEvent& e : rec->history().events()) {
        if (!e.finished()) continue;
        if (e.at - last > kSessionGap) {
            together(session, 1);
            session.clear();
        }
        last = e.at;
        add(session, e.track);
    }
    together(session, 1);

    const QVariantList main = library->tracks();
    for (int from = 0; from < main.size(); from += kLikeWindow / 2) {
        QStringList window;
        for (int i = from; i < qMin(from + kLikeWindow, int(main.size())); ++i) add(window, main[i].toMap());
        together(window, kLikeWeight);
    }

    // метки это ключи артистов; обход и выбор при равенстве по порядку любви,
    // чтобы группы не менялись от запуска к запуску
    QHash<QString, QString> label;
    for (const QString& a : std::as_const(artists)) label.insert(a, a);
    for (int round = 0; round < 10; ++round) {
        bool changed = false;
        for (const QString& a : std::as_const(artists)) {
            QHash<QString, double> votes;
            votes[label[a]] += kInertia;
            const QHash<QString, double>& neighbours = link[a];
            for (auto it = neighbours.cbegin(); it != neighbours.cend(); ++it) votes[label[it.key()]] += it.value();
            QString best = label[a];
            for (const QString& candidate : std::as_const(artists)) {
                if (votes.value(candidate) > votes.value(best) + 1e-9) best = candidate;
            }
            if (best != label[a]) {
                label[a] = best;
                changed = true;
            }
        }
        if (!changed) break;
    }

    QStringList order;
    QHash<QString, QStringList> byLabel;
    for (const QString& a : std::as_const(artists)) {
        if (!byLabel.contains(label[a])) order.append(label[a]);
        byLabel[label[a]].append(a);
    }
    QList<QStringList> groups;
    for (const QString& l : std::as_const(order)) {
        if (byLabel[l].size() >= 2) groups.append(byLabel[l]);
    }
    auto weight = [&affinity](const QStringList& g) {
        double sum = 0;
        for (const QString& a : g) sum += affinity.value(a);
        return sum;
    };
    std::stable_sort(groups.begin(), groups.end(),
                     [&weight](const QStringList& a, const QStringList& b) { return weight(a) > weight(b); });
    if (groups.size() > kMaxGroups) groups.resize(kMaxGroups);
    return groups;
}

void MorphMixes::buildDailyMix(int index) {
    const QList<QStringList> groups = artistGroups();
    if (index < 0 || index >= groups.size()) {
        emit failed("This mix has changed, open the home screen again");
        return;
    }

    QSet<QString> group(groups[index].cbegin(), groups[index].cend());
    QVariantList own;
    for (const QVariant& t : familiar()) {
        if (group.contains(TrackMatch::artistKey(t.toMap()))) own.append(t);
    }

    QVariantList seedList;
    QSet<QString> used;
    for (const QVariant& v : std::as_const(own)) {
        QVariantMap t = v.toMap();
        QString artist = TrackMatch::artistKey(t);
        if (seedList.size() >= 3) break;
        if (used.contains(artist) || yandexIdOf(t).isEmpty()) continue;
        used.insert(artist);
        seedList.append(t);
    }

    QString name = QString("Daily Mix %1").arg(index + 1);
    similarTo(seedList, 1, [this, own, name](const QVariantList& similar) {
        QVariantList candidates;
        QSet<QString> seen;
        qint64 now = rec->now();
        for (const QVariant& v : own + similar) {
            QVariantMap t = v.toMap();
            QString key = TrackMatch::songKey(t);
            if (seen.contains(key) || rec->profile().affinityOf(t, now) < kDislikedArtist) continue;
            seen.insert(key);
            candidates.append(t);
        }
        QVariantList tracks = rec->rank(candidates, kMixSize, false, kMixNewShare);
        emit ready(name, card({}, {}, {}, own)["coverUrl"].toString(), tracks);
    });
}

void MorphMixes::buildDiscover() {
    QFile cached(m_discoverPath);
    if (cached.open(QIODevice::ReadOnly)) {
        QJsonObject saved = QJsonDocument::fromJson(cached.readAll()).object();
        QVariantList tracks = saved["tracks"].toArray().toVariantList();
        if (saved["week"].toInt() == week() && !tracks.isEmpty()) {
            emit ready("Discover Weekly", saved["cover"].toString(), tracks);
            return;
        }
    }

    QVariantList seedList = seeds(8);
    if (seedList.isEmpty()) {
        emit failed("Finish or like a few Yandex tracks first");
        return;
    }
    similarTo(seedList, 2, [this](const QVariantList& similar) {
        qint64 now = rec->now();
        QVariantList fresh;
        QSet<QString> seen;
        for (const QVariant& v : similar) {
            QVariantMap t = v.toMap();
            QString key = TrackMatch::songKey(t);
            if (seen.contains(key) || !rec->isNew(t) || rec->profile().affinityOf(t, now) < 0) continue;
            seen.insert(key);
            fresh.append(t);
        }

        // витрина новых имён: по треку на артиста
        QVariantList tracks;
        QSet<QString> artists;
        for (const QVariant& v : rec->rank(fresh, int(fresh.size()), false, 1.0)) {
            QString artist = TrackMatch::artistKey(v.toMap());
            if (artists.contains(artist)) continue;
            artists.insert(artist);
            tracks.append(v);
            if (tracks.size() >= kListSize) break;
        }
        if (tracks.isEmpty()) {
            emit failed("Nothing new this time, try again later");
            return;
        }

        QString cover = card({}, {}, {}, tracks)["coverUrl"].toString();
        QFile file(m_discoverPath);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonObject saved{{"week", week()}, {"cover", cover}, {"tracks", QJsonArray::fromVariantList(tracks)}};
            file.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
        }
        emit ready("Discover Weekly", cover, tracks);
    });
}

// Свежие альбомы и синглы любимых артистов, новые первыми
void MorphMixes::buildReleaseRadar() {
    const QHash<QString, QString> ids = artistIds();
    QStringList wanted;
    for (const auto& [key, affinity] : rec->profile().top(kGroupArtists, rec->now())) {
        if (ids.contains(key)) wanted.append(ids[key]);
        if (wanted.size() >= kReleaseArtists) break;
    }
    if (wanted.isEmpty()) {
        emit failed("Like a few Yandex tracks first");
        return;
    }

    struct Gather {
        int pending = 0;
        QVariantList albums;
    };
    auto gather = std::make_shared<Gather>();
    gather->pending = wanted.size();
    QDateTime since = QDateTime::fromSecsSinceEpoch(rec->now() - kReleaseDays * kDay);
    for (const QString& id : std::as_const(wanted)) {
        yandex->artistAlbums(id, [this, gather, since](const QVariantList& albums) {
            for (const QVariant& a : albums) {
                QDateTime released = QDateTime::fromString(a.toMap()["releaseDate"].toString(), Qt::ISODate);
                if (released.isValid() && released >= since) gather->albums.append(a);
            }
            if (--gather->pending > 0) return;

            QVariantList albumsByDate = gather->albums;
            std::stable_sort(albumsByDate.begin(), albumsByDate.end(), [](const QVariant& a, const QVariant& b) {
                return a.toMap()["releaseDate"].toString() > b.toMap()["releaseDate"].toString();
            });
            QStringList albumIds;
            for (const QVariant& a : std::as_const(albumsByDate)) {
                QString albumId = a.toMap()["id"].toString();
                if (!albumIds.contains(albumId)) albumIds.append(albumId);
            }
            albumIds = albumIds.mid(0, kReleaseAlbums);
            if (albumIds.isEmpty()) {
                emit failed(QString("No new releases from your artists in the last %1 days").arg(kReleaseDays));
                return;
            }

            QString cover = albumsByDate.first().toMap()["coverUrl"].toString();
            auto parts = std::make_shared<QList<QVariantList>>(albumIds.size());
            auto left = std::make_shared<int>(albumIds.size());
            for (int i = 0; i < albumIds.size(); ++i) {
                yandex->albumTracks(albumIds[i], [this, parts, left, i, cover](const QVariantList& tracks) {
                    (*parts)[i] = tracks;
                    if (--*left > 0) return;
                    QVariantList all;
                    for (const QVariantList& p : std::as_const(*parts)) all += p;
                    emit ready("Release Radar", cover, all);
                });
            }
        });
    }
}

void MorphMixes::similarTo(const QVariantList& seeds, int soundcloudSeeds, Tracks done) {
    QStringList yandexIds;
    for (const QVariant& s : seeds) {
        QString id = yandexIdOf(s.toMap());
        if (!id.isEmpty()) yandexIds.append(id);
    }
    int fromSoundCloud = soundcloud->hasToken() ? qMin(soundcloudSeeds, int(seeds.size())) : 0;

    struct Gather {
        int pending = 0;
        QVariantList found;
    };
    auto gather = std::make_shared<Gather>();
    gather->pending = yandexIds.size() + fromSoundCloud;
    if (gather->pending == 0) {
        done({});
        return;
    }
    auto collect = [gather, done](const QVariantList& tracks) {
        gather->found += tracks;
        if (--gather->pending == 0) done(gather->found);
    };
    for (const QString& id : std::as_const(yandexIds)) yandex->similarTracks(id, collect);
    for (int i = 0; i < fromSoundCloud; ++i)
        soundcloud->relatedTo(TrackMatch::searchQuery(seeds[i].toMap()), 10,
                              [this, collect](const QVariantList& tracks) {
                                  // похожее с SoundCloud сырое, через тот же отбор, что и в волне
                                  if (filter) filter->process(tracks, "SoundCloud", collect);
                                  else collect(tracks);
                              });
}

// По названию-заглушке вроде My Playlist #3 искать нечего
static bool saysSomething(const QString& name) {
    static const QRegularExpression placeholder("^my playlist( #?\\d+)?$", QRegularExpression::CaseInsensitiveOption);
    QString clean = name.trimmed();
    return !clean.isEmpty() && !placeholder.match(clean).hasMatch();
}

void MorphMixes::suggest(const QString& name, const QVariantList& tracks, int count, Suggestions done) {
    QSet<QString> present;
    for (const QVariant& t : tracks) present.insert(TrackMatch::songKey(t.toMap()));

    auto finish = [this, present, count, done](const QVariantList& found, const QString& basis) {
        QVariantList fresh;
        QSet<QString> seen = present;
        for (const QVariant& v : found) {
            QVariantMap t = v.toMap();
            // в больших плейлистах Яндекс отдаёт часть треков без данных
            if (t["title"].toString().isEmpty()) continue;
            QString key = TrackMatch::songKey(t);
            if (seen.contains(key)) continue;
            seen.insert(key);
            fresh.append(t);
        }
        done(rec->rank(fresh, count), basis);
    };

    if (!tracks.isEmpty()) {
        // образцы случайные, поэтому REFRESH даёт другое. Треки с версией в
        // Яндексе вперёд: по ним есть похожее
        QVariantList pick = tracks;
        std::shuffle(pick.begin(), pick.end(), *QRandomGenerator::global());
        std::stable_partition(pick.begin(), pick.end(),
                              [this](const QVariant& t) { return !yandexIdOf(t.toMap()).isEmpty(); });
        similarTo(pick.mid(0, kSuggestSeeds), 1, [finish](const QVariantList& found) { finish(found, "contents"); });
        return;
    }
    if (saysSomething(name)) {
        byTitle(name, [finish](const QVariantList& found) { finish(found, "title"); });
        return;
    }
    QVariantList favourites = seeds(kSuggestSeeds);
    if (favourites.isEmpty()) {
        done({}, "taste");
        return;
    }
    similarTo(favourites, 1, [finish](const QVariantList& found) { finish(found, "taste"); });
}

// Плейлисты с таким же названием собраны людьми под то же настроение, поэтому
// их треки ближе к задумке, чем песни, у которых это просто в названии. Без
// Яндекса остаётся поиск по SoundCloud
void MorphMixes::byTitle(const QString& name, Tracks done) {
    if (!yandex->hasToken()) {
        soundcloud->searchTracks(name, kListSize, [this, done](const QVariantList& found) {
            if (filter) filter->process(found, "SoundCloud", done);
            else done(found);
        });
        return;
    }

    // два поиска, а потом ещё треки каждого найденного плейлиста
    struct Gather {
        int pending = 2;
        QVariantList found;
    };
    auto gather = std::make_shared<Gather>();
    auto collect = [gather, done](const QVariantList& tracks) {
        gather->found += tracks;
        if (--gather->pending == 0) done(gather->found);
    };
    yandex->searchTracks(name, collect);
    yandex->searchPlaylists(name, [this, gather, collect](const QVariantList& playlists) {
        int take = qMin(kTitlePlaylists, int(playlists.size()));
        gather->pending += take;
        for (int i = 0; i < take; ++i) {
            QVariantMap p = playlists[i].toMap();
            yandex->playlistTracks(p["uid"].toString(), p["kind"].toString(), collect);
        }
        collect({});
    });
}

QVariantMap MorphMixes::card(const QString& id, const QString& name, const QString& subtitle,
                             const QVariantList& tracks) {
    QString cover;
    for (const QVariant& t : tracks) {
        cover = t.toMap()["coverUrl"].toString();
        if (!cover.isEmpty()) break;
    }
    return {{"id", id},          {"name", name},       {"subtitle", subtitle},
            {"coverUrl", cover}, {"service", "morph"}, {"permalink_url", id}};
}

// номер недели с начала эпохи: Discover Weekly и Rediscover меняются по понедельникам
int MorphMixes::week() const {
    // 1 января 1970 был четверг, сдвиг на три дня ставит границу на понедельник
    return int((rec->now() / kDay + 3) / 7);
}
