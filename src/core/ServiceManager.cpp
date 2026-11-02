#include "ServiceManager.h"
#include "../services/TrackData.h"
#include "TrackMatch.h"
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QUrl>

// связи с Яндексом ищутся в фоне: столько за запуск и с такой паузой
static const int kLinksPerRun = 40;
// лайков в SoundCloud за запуск: тысяча песен MAIN разойдётся за несколько дней
static const int kSoundCloudWritesPerRun = 60;
static const int kLinkPauseMs = 1500;
// та же песня с уверенностью, как у фолбэка: иначе лайк уйдёт не тому треку
static const double kLinkScore = 0.85;

ServiceManager::ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent)
    : QObject(parent), net(net), cache(cache) {
    connect(net, &NetworkManager::errorOccurred, this, &ServiceManager::errorOccurred);
    yandex = new YandexService(net, this);
    soundcloud = new SoundCloudService(net, this);
    youtube = new YouTubeService(this);
    mixer = new WaveMixer(soundcloud, youtube, this);
    m_filter = new CandidateFilter(yandex, PathProvider::getDataPath() + "/candidates.json", this);
    mixer->setFilter(m_filter);

    m_services = {{"Yandex", yandex}, {"SoundCloud", soundcloud}, {"YouTube", youtube}};
    for (const auto& [name, s] : m_services) attach(s);

    // трек убрали из Яндекса: та же песня с SoundCloud или YouTube, но под
    // яндексовским id, так QML ничего не замечает, а кэш потом отдаёт её офлайн
    finder = new FallbackFinder(soundcloud, youtube, PathProvider::getDataPath() + "/fallback_choices.json", this);
    connect(yandex, &YandexService::streamUnavailable, this,
            [this](const QString& trackId, const QString& artist, const QString& title, qint64 durationMs) {
                QVariantMap wanted{{"artist", artist}, {"title", title}, {"durationMs", durationMs}};
                finder->find(trackId, wanted, m_prefetching.contains(trackId));
            });
    connect(youtube, &YouTubeService::resolveFailed, finder, &FallbackFinder::streamFailed);
    connect(soundcloud, &SoundCloudService::streamFailed, finder, &FallbackFinder::streamFailed);
    connect(finder, &FallbackFinder::chosen, this,
            [this](const QString& trackId, const QVariantMap& candidate, int index, int total) {
                QVariantMap source = candidate;
                source["index"] = index + 1;
                source["total"] = total;
                emit fallbackChosen(trackId, source);
            });
    connect(finder, &FallbackFinder::notFound, this, [this](const QString& trackId, const QString& query) {
        // про трек, который ещё не включали, молчим: включат, тогда и скажем
        if (!m_prefetching.remove(trackId)) emit errorOccurred("Not found on SoundCloud or YouTube: " + query);
    });
    connect(finder, &FallbackFinder::noOtherVersion, this, [this]() { emit errorOccurred("No other versions found"); });
    // MAIN: версии той же песни с других площадок пробуются раньше поиска
    m_library = new Library(PathProvider::getDataPath() + "/library.json", this);
    m_library->setAvailability([this](const QVariantMap& t) {
        return t["service"].toString() != "Yandex" || yandex->isAvailable(t["id"].toString());
    });
    finder->setYandex(yandex);
    finder->setKnownSources([this](const QString& trackId) { return m_library->alternatives(trackId); });
    connect(yandex, &YandexService::streamFailed, finder, &FallbackFinder::streamFailed);
    // после finder: его заменам не помогли, а это сам трек не заиграл
    connect(soundcloud, &SoundCloudService::streamFailed, this, &ServiceManager::directStreamFailed);
    connect(youtube, &YouTubeService::resolveFailed, this, &ServiceManager::directStreamFailed);
    m_linkTimer = new QTimer(this);
    m_linkTimer->setSingleShot(true);
    m_linkTimer->setInterval(kLinkPauseMs);
    connect(m_linkTimer, &QTimer::timeout, this, &ServiceManager::linkNext);
    connect(m_library, &Library::changed, m_linkTimer, qOverload<>(&QTimer::start));

    m_recommender = new Recommender(PathProvider::getDataPath(), this);
    connect(m_library, &Library::changed, m_recommender, [this]() { m_recommender->setLikes(m_library->tracks()); });
    m_mixes = new MorphMixes(m_recommender, m_library, yandex, soundcloud, PathProvider::getDataPath(), this);
    m_mixes->setFilter(m_filter);
    connect(m_mixes, &MorphMixes::ready, this,
            [this](const QString& name, const QString& coverUrl, const QVariantList& tracks) {
                if (!coverUrl.isEmpty()) this->cache->cacheCover(coverUrl);
                emit playlistImported(name, coverUrl, tracks);
            });
    connect(m_mixes, &MorphMixes::failed, this, &ServiceManager::errorOccurred);
    connect(m_mixes, &MorphMixes::seededBatch, this, [this](const QVariantList& tracks) {
        emit waveReady("morph", m_smartWaves ? m_recommender->nextWave(tracks) : tracks);
    });

    // порядок волны решает своя модель, миксер только поставляет кандидатов
    connect(mixer, &WaveMixer::waveReady, this, [this](const QString& name, const QVariantList& tracks) {
        emit waveReady(name, m_smartWaves ? m_recommender->nextWave(tracks) : tracks);
    });

    connect(this, &ServiceManager::streamUrlReady, this, [this](const QString& trackId, const QString& url) {
        m_direct.remove(trackId);
        // поток с YouTube через VPN начинает играть заметно позже яндексовского,
        // поэтому следующий трек сразу ложится в кэш и потом стартует с диска
        if (m_prefetching.remove(trackId) && url.contains("googlevideo.com") && this->cache->savesTracks()) {
            this->cache->cacheTrack(trackId, url);
        }
    });

    lyrics = new LyricsService(net, this);
    connect(lyrics, &LyricsService::lyricsReady, this, &ServiceManager::lyricsReady);
    connect(lyrics, &LyricsService::lyricsMissing, this, &ServiceManager::lyricsMissing);
    connect(yandex, &YandexService::stationsReady, this, &ServiceManager::stationsReady);
    connect(yandex, &YandexService::waveSettingsReady, this, [this](QVariantList options) {
        // своя настройка рядом с яндексовскими: те уходят сидами в сессию,
        // эта меняет долю незнакомых артистов в ранжировании
        QVariantList values{QVariantMap{{"name", "Familiar"}, {"seed", "morph:familiar"}},
                            QVariantMap{{"name", "Balanced"}, {"seed", "morph:balanced"}, {"unspecified", true}},
                            QVariantMap{{"name", "New"}, {"seed", "morph:new"}}};
        options.append(QVariantMap{{"key", "morphDiscovery"}, {"name", "morph: familiar or new"}, {"values", values}});
        emit waveSettingsReady(options);
    });
    connect(yandex, &YandexService::likesFetched, this, [this](const QVariantList& tracks, const QStringList& allIds) {
        emit likesFetched("Yandex", tracks, allIds);

        QVariantList lost;
        for (const QVariant& t : tracks) {
            if (!yandex->isAvailable(t.toMap()["id"].toString())) lost.append(t);
        }
        mixer->setLostTracks(lost);
    });
}

// Раньше тут было по три одинаковых connect на каждый сигнал, по одному на сервис.
// Теперь сервис подключается один раз, а новый добавляется одной строкой в m_services
void ServiceManager::attach(BaseService* s) {
    auto withDuration = [](QVariantList tracks) {
        for (QVariant& t : tracks) {
            QVariantMap track = t.toMap();
            if (!track.contains("durationMs")) track["durationMs"] = 0;
            t = track;
        }
        return tracks;
    };

    connect(s, &BaseService::searchResultsReady, this,
            [this, withDuration](const QString& name, const QVariantList& r) {
                emit searchResultsReady(name, withDuration(r));
            });
    connect(s, &BaseService::chartsReady, this, [this, withDuration](const QString& name, const QVariantList& r) {
        emit chartsReady(name, withDuration(r));
    });
    // партии волны уходят в QML через миксер, он подмешивает SoundCloud и YouTube
    connect(s, &BaseService::waveReady, this, [this, withDuration](const QString& name, const QVariantList& r) {
        mixer->addBatch(name, withDuration(r));
    });
    connect(s, &BaseService::dailyMixesReady, this, [this](const QString& name, const QVariantList& playlists) {
        for (const QVariant& pl : playlists) {
            QString coverUrl = pl.toMap()["coverUrl"].toString();
            if (!coverUrl.isEmpty()) cache->cacheCover(coverUrl);
        }
        emit dailyMixesReady(name, playlists);
    });
    connect(s, &BaseService::playlistImported, this,
            [this, withDuration](const QString& name, const QString& coverUrl, const QVariantList& tracks) {
                if (!coverUrl.isEmpty()) cache->cacheCover(coverUrl);
                emit playlistImported(name, coverUrl, withDuration(tracks));
            });

    connect(s, &BaseService::streamUrlReady, this, &ServiceManager::streamUrlReady);
    connect(s, &BaseService::bitrateReady, this, &ServiceManager::bitrateReady);
    connect(s, &BaseService::errorOccurred, this, &ServiceManager::errorOccurred);
}

BaseService* ServiceManager::service(const QString& name) const {
    for (const auto& [serviceName, s] : m_services) {
        if (serviceName.compare(name, Qt::CaseInsensitive) == 0) return s;
    }
    return nullptr;
}

void ServiceManager::search(const QString& query, const QString& serviceName) {
    for (const auto& [name, s] : m_services) {
        if (serviceName == "all" || name.compare(serviceName, Qt::CaseInsensitive) == 0) s->search(query);
    }
}

void ServiceManager::getCharts() {
    for (const auto& [name, s] : m_services) s->getCharts();
}

void ServiceManager::getWave() {
    if (m_seededWave) {
        m_mixes->nextSeeded();
        return;
    }
    for (const auto& [name, s] : m_services) s->getWave();
}

void ServiceManager::startWave(const QStringList& seeds) {
    m_seededWave = false;
    m_recommender->resetWave();
    yandex->startWave(seeds);
}

void ServiceManager::startTrackWave(const QVariantMap& track) {
    m_seededWave = false;
    m_recommender->resetWave();
    yandex->startTrackWave(track);
}

void ServiceManager::startArtistWave(const QString& artistName) {
    m_seededWave = false;
    m_recommender->resetWave();
    yandex->startArtistWave(artistName);
}

void ServiceManager::startPlaylistWave(const QVariantList& tracks) {
    if (tracks.isEmpty()) return;
    m_seededWave = true;
    m_recommender->resetWave();
    m_mixes->startSeeded(tracks);
}

QVariantList ServiceManager::arrangeTracks(const QVariantList& tracks, const QString& filter,
                                           const QString& sort) const {
    auto normalized = [](QString text) { return text.toLower().replace(u'ё', u'е'); };
    const QStringList words = normalized(filter).split(' ', Qt::SkipEmptyParts);
    QVariantList out;
    for (const QVariant& v : tracks) {
        QVariantMap t = v.toMap();
        QString where = normalized(t["title"].toString() + " " + t["artist"].toString() + " " + t["album"].toString());
        if (std::all_of(words.cbegin(), words.cend(), [&where](const QString& w) { return where.contains(w); }))
            out.append(v);
    }

    if (sort == "plays") {
        const auto songs = m_recommender->history().songs();
        auto plays = [&songs](const QVariant& t) { return songs.value(TrackMatch::songKey(t.toMap())).plays; };
        std::stable_sort(out.begin(), out.end(),
                         [&plays](const QVariant& a, const QVariant& b) { return plays(a) > plays(b); });
    } else if (sort == "duration") {
        std::stable_sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap()["durationMs"].toLongLong() < b.toMap()["durationMs"].toLongLong();
        });
    } else if (sort == "title" || sort == "artist") {
        // по алфавиту, а где системная сортировка умеет (на маке, с ICU), цифры как
        // числа: «Трек 2» перед «Трек 10»
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::stable_sort(out.begin(), out.end(), [&collator, &sort](const QVariant& a, const QVariant& b) {
            return collator.compare(a.toMap()[sort].toString(), b.toMap()[sort].toString()) < 0;
        });
    }
    return out;
}

void ServiceManager::setDiscovery(const QString& level) {
    m_recommender->setNewShare(level == "new" ? 0.5 : level == "familiar" ? 0.15 : 0.3);
}

void ServiceManager::setWaveSettings(const QStringList& settingSeeds) {
    yandex->setWaveSettings(settingSeeds);
}

void ServiceManager::fetchWaveStations() {
    yandex->fetchWaveStations();
}

void ServiceManager::fetchLyrics(const QVariantMap& track) {
    if (!track["id"].toString().isEmpty()) lyrics->fetch(track);
}

void ServiceManager::fetchArtist(const QString& name, const QVariantMap& track) {
    QString wanted = name.trimmed();
    if (wanted.isEmpty()) return;

    // без Яндекса или если его там нет: что нашлось на SoundCloud под этим именем
    auto fromSoundCloud = [this, wanted]() {
        soundcloud->searchTracks(wanted, 30, [this, wanted](const QVariantList& tracks) {
            QString key = TrackMatch::artistKey({{"artist", wanted}});
            QVariantList own;
            for (const QVariant& t : tracks) {
                if (TrackMatch::artistKey(t.toMap()) == key) own.append(t);
            }
            deliverArtist(wanted, {{"name", wanted}, {"popular", own.isEmpty() ? tracks : own}});
        });
    };
    auto fromYandex = [this, wanted, fromSoundCloud](const QString& artistId) {
        if (artistId.isEmpty()) {
            fromSoundCloud();
            return;
        }
        yandex->artistInfo(artistId, [this, wanted, fromSoundCloud](const QVariantMap& info) {
            if (info.isEmpty()) fromSoundCloud();
            else deliverArtist(wanted, info);
        });
    };

    // id есть у трека из Яндекса, но только для первого артиста, а нажать могли на гостя
    bool mainArtist = TrackMatch::artistKey(track) == TrackMatch::artistKey({{"artist", wanted}});
    QString artistId = mainArtist ? track["artistId"].toString() : QString();
    if (!artistId.isEmpty()) fromYandex(artistId);
    else yandex->searchArtist(wanted, fromYandex);
}

// Своё к странице артиста: его песни в MAIN и сколько раз он играл
void ServiceManager::deliverArtist(const QString& requested, QVariantMap info) {
    QString key = TrackMatch::artistKey({{"artist", info["name"].toString()}});
    QVariantList yours;
    for (const QVariant& t : m_library->tracks()) {
        if (TrackMatch::artistKey(t.toMap()) == key) yours.append(t);
    }
    info["requestedName"] = requested;
    info["inMain"] = yours.size();
    info["yours"] = yours.mid(0, 20);
    info["plays"] = m_recommender->profile().heard(key);
    emit artistReady(info);
}

void ServiceManager::openAlbum(const QVariantMap& album) {
    yandex->albumTracks(album["id"].toString(), [this, album](const QVariantList& tracks) {
        if (tracks.isEmpty()) {
            emit errorOccurred("Album is unavailable: " + album["title"].toString());
            return;
        }
        QString coverUrl = album["coverUrl"].toString();
        if (!coverUrl.isEmpty()) cache->cacheCover(coverUrl);
        emit playlistImported(album["title"].toString(), coverUrl, tracks);
    });
}

void ServiceManager::getDailyMixes() {
    for (const auto& [name, s] : m_services) s->getDailyMixes();
    // свои миксы считаются на месте, треки по клику через importPlaylist
    QVariantList cards = m_mixes->cards();
    for (const QVariant& c : std::as_const(cards)) {
        QString coverUrl = c.toMap()["coverUrl"].toString();
        if (!coverUrl.isEmpty()) cache->cacheCover(coverUrl);
    }
    emit dailyMixesReady("morph", cards);
}

void ServiceManager::resolve(const QString& serviceName, const QString& trackId) {
    m_prefetching.remove(trackId);
    BaseService* s = service(serviceName);
    if (cache->isTrackCached(trackId) || (s && s->isTrackCached(trackId))) {
        emit streamUrlReady(trackId, cache->getTrackUrl(trackId));
        return;
    }

    // строки из импортированного .txt ищутся так же, как убранные из Яндекса
    QString query = YouTubeService::queryFromTrackId(trackId);
    if (s == youtube && !query.isEmpty()) {
        finder->find(trackId, TrackMatch::fromQuery(query), false);
        return;
    }
    if (s == soundcloud || s == youtube) m_direct.insert(trackId);
    if (s) s->resolveStreamUrl(trackId);
}

// Следующий трек ищется, пока играет текущий. Яндекс и SoundCloud отвечают
// одним быстрым запросом, а yt-dlp встаёт в очередь за тем, что уже включено
void ServiceManager::prefetch(const QString& serviceName, const QString& trackId) {
    BaseService* s = service(serviceName);
    if (!s || trackId.isEmpty() || cache->isTrackCached(trackId) || s->isTrackCached(trackId)) return;

    m_prefetching.insert(trackId);
    QString query = YouTubeService::queryFromTrackId(trackId);
    if (s == youtube && !query.isEmpty()) {
        finder->find(trackId, TrackMatch::fromQuery(query), true);
        return;
    }
    if (s == soundcloud || s == youtube) m_direct.insert(trackId);
    if (s == youtube) youtube->prefetch(trackId);
    else s->resolveStreamUrl(trackId);
}

// Трек с SoundCloud или YouTube удалили или закрыли. Если песня есть в MAIN,
// её играет другая версия: лайкнутая, найденная в Яндексе или поиском
void ServiceManager::directStreamFailed(const QString& trackId) {
    if (!m_direct.remove(trackId) || finder->knows(trackId)) return;
    QVariantMap track = knownTrack(trackId);
    if (!track.isEmpty()) finder->find(trackId, wantedFrom(track), m_prefetching.contains(trackId));
}

QVariantMap ServiceManager::knownTrack(const QString& trackId) const {
    QVariantMap track = m_started.value(trackId);
    return track.isEmpty() ? m_library->trackById(trackId) : track;
}

// Песни из MAIN без версии в Яндексе ищутся там по одной и с паузой, чтобы
// сотни лайков с SoundCloud не ушли в Яндекс одной пачкой
// Фоновая сверка MAIN с площадками, по одному запросу на тик: сначала версии
// в Яндексе, потом лайки на SoundCloud и поиск версий там
void ServiceManager::linkNext() {
    if (linkYandex()) return;
    syncSoundCloud();
}

bool ServiceManager::linkYandex() {
    if (m_yandexToken.isEmpty() || m_linksThisRun >= kLinksPerRun) return false;
    QString key = m_library->nextToLink();
    if (key.isEmpty()) return false;

    QVariantMap track = m_library->representative(key);
    m_library->markLinkTried(key);
    ++m_linksThisRun;
    yandex->searchTracks(TrackMatch::searchQuery(track), [this, key, track](const QVariantList& found) {
        QVariantMap best;
        double bestScore = kLinkScore;
        for (const QVariant& v : found) {
            // в Яндексе подписи чистые, поэтому он «искомый», а трек с SoundCloud
            // «кандидат»: у кандидата артист может сидеть в названии
            double score = TrackMatch::score(v.toMap(), track);
            if (score >= bestScore) {
                bestScore = score;
                best = v.toMap();
            }
        }
        if (!best.isEmpty()) {
            m_library->link(key, best);
            if (m_syncToYandex) yandex->setLiked(best["id"].toString(), true);
        }
        m_linkTimer->start();
    });
    return true;
}

// MAIN лайками на SoundCloud: тогда его related и миксы подбирают под тебя.
// Это сверка, а не события: что не успело из-за протухшего токена, уйдёт в
// следующий раз, когда токен свежий
bool ServiceManager::syncSoundCloud() {
    if (!m_syncToSoundCloud || !soundcloud->canWrite() || m_soundcloudWrites >= kSoundCloudWritesPerRun) return false;

    // не вышло, значит токен истёк или SoundCloud против: до следующего запуска
    auto written = [this](const QString& key, bool on) {
        return [this, key, on](bool ok) {
            if (ok) m_library->setMirrored("SoundCloud", key, on);
            else m_soundcloudWrites = kSoundCloudWritesPerRun;
            m_linkTimer->start();
        };
    };

    const QStringList mirrored = m_library->mirrored("SoundCloud");
    for (const QString& key : mirrored) {
        if (m_library->hasSong(key)) continue;
        // песню убрали из MAIN: снимаем лайк, который ставил morph
        QString id = m_library->sourceId(key, "SoundCloud");
        if (id.isEmpty()) {
            m_library->setMirrored("SoundCloud", key, false);
            continue;
        }
        ++m_soundcloudWrites;
        soundcloud->setLiked(id, false, written(key, false));
        return true;
    }

    const QStringList keys = m_library->songKeys();
    for (const QString& key : keys) {
        QVariantMap source = m_library->source(key, "SoundCloud");
        if (mirrored.contains(key) || source.isEmpty()) continue;
        // id должен пережить снятый лайк, иначе потом нечего будет снимать
        m_library->link(key, source);
        ++m_soundcloudWrites;
        soundcloud->setLiked(source["id"].toString(), true, written(key, true));
        return true;
    }

    if (m_soundcloudSearches >= kLinksPerRun) return false;
    QString key = m_library->nextToLink("SoundCloud");
    if (key.isEmpty()) return false;
    QVariantMap track = m_library->representative(key);
    m_library->markLinkTried(key, "SoundCloud");
    ++m_soundcloudSearches;
    soundcloud->searchTracks(TrackMatch::searchQuery(track), 10, [this, key, track](const QVariantList& found) {
        QVariantMap best;
        double bestScore = kLinkScore;
        for (const QVariant& v : found) {
            double score = TrackMatch::score(track, v.toMap());
            if (score >= bestScore) {
                bestScore = score;
                best = v.toMap();
            }
        }
        if (!best.isEmpty()) m_library->link(key, best);
        m_linkTimer->start();
    });
    return true;
}

void ServiceManager::setSyncToSoundCloud(bool enabled) {
    bool turnedOn = enabled && !m_syncToSoundCloud;
    m_syncToSoundCloud = enabled;
    if (turnedOn) m_linkTimer->start();
}

// Играет не та версия: файл в кэше от прошлой выкидываем, иначе QML снова
// включит его, и берём следующий вариант по очкам
void ServiceManager::otherVersion(const QVariantMap& track) {
    QString trackId = track["id"].toString();
    if (trackId.isEmpty()) return;
    cache->forgetTrack(trackId);
    m_prefetching.remove(trackId);
    finder->nextVersion(trackId, wantedFrom(track));
}

// Чем заменён трек, для значка в плеере. Выбор запоминается только для
// треков, которые Яндекс не отдал, и для строк из .txt, поэтому у остальных
// его просто нет. После перезапуска трек играет из кэша, и замена известна
// только по запомненному выбору
QVariantMap ServiceManager::fallbackSource(const QVariantMap& track) const {
    QString trackId = track["id"].toString();
    bool replaceable = track["service"].toString() == "Yandex" || !YouTubeService::queryFromTrackId(trackId).isEmpty();
    if (!replaceable) return {};
    return finder->currentChoice(trackId, wantedFrom(track));
}

QVariantMap ServiceManager::wantedFrom(const QVariantMap& track) {
    QString query = YouTubeService::queryFromTrackId(track["id"].toString());
    if (!query.isEmpty()) return TrackMatch::fromQuery(query);
    return {{"artist", track["artist"]}, {"title", track["title"]}, {"durationMs", track["durationMs"]}};
}

void ServiceManager::reportPlay(const QString& serviceName, const QString& trackId, const QString& albumId) {
    emit trackStarted(serviceName, trackId, albumId);
    if (BaseService* s = service(serviceName)) s->reportPlay(trackId, albumId);
}

void ServiceManager::nowPlaying(const QVariantMap& track) {
    QString trackId = track["id"].toString();
    // трек, который так и не доиграли (выход из приложения), не должен копиться
    if (m_started.size() > 50) m_started.clear();
    if (!trackId.isEmpty()) m_started.insert(trackId, track);
    reportPlay(track["service"].toString(), trackId, track["album"].toString());
}

void ServiceManager::reportListen(const QString& serviceName, const QString& trackId, const QString& albumId,
                                  double playedSeconds, double lengthSeconds, bool completed) {
    Q_UNUSED(albumId);
    QVariantMap track = knownTrack(trackId);
    if (!track.isEmpty()) m_recommender->listened(track, playedSeconds, lengthSeconds, completed);
    m_started.remove(trackId);
    if (completed) mixer->trackFinished(trackId);
    if (serviceName == "Yandex") {
        yandex->reportListen(trackId, playedSeconds, lengthSeconds, completed);
        return;
    }
    // прослушивание с SoundCloud засчитывается версии в Яндексе: его волна учится и на нём
    QString yandexId = m_syncToYandex && !track.isEmpty() ? m_library->yandexId(TrackMatch::songKey(track)) : QString();
    if (!yandexId.isEmpty()) yandex->reportListen(yandexId, playedSeconds, lengthSeconds, completed);
}

void ServiceManager::setLiked(const QVariantMap& track, bool liked) {
    m_recommender->liked(track, liked);
    if (liked) mixer->addSeed(track);
    if (track["service"].toString() == "Yandex") {
        yandex->setLiked(track["id"].toString(), liked);
        return;
    }
    // связь знает и песню, которую только что убрали из MAIN
    QString yandexId = m_syncToYandex ? m_library->yandexId(TrackMatch::songKey(track)) : QString();
    if (!yandexId.isEmpty()) yandex->setLiked(yandexId, liked);
}

void ServiceManager::dislike(const QVariantMap& track) {
    m_recommender->disliked(track);
    if (track["service"].toString() == "Yandex") yandex->dislike(track["id"].toString());
}

void ServiceManager::importPlaylist(const QString& url) {
    // вместо ссылки можно вставить путь к txt со строками "артист - название"
    QString path = url.trimmed();
    if (path.startsWith("file://")) path = QUrl(path).toLocalFile();
    if (path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
    if (QFileInfo(path).isFile()) {
        importTextFile(path);
        return;
    }

    if (url.startsWith("morph:")) {
        m_mixes->build(url);
    } else if (url.contains("yandex")) {
        yandex->importPlaylist(url);
    } else if (url.contains("soundcloud")) {
        soundcloud->importPlaylist(url);
    } else if (url.contains("youtube") || url.contains("youtu.be")) {
        youtube->importPlaylist(url);
    } else {
        emit errorOccurred("Unsupported URL. Use Yandex, SoundCloud or YouTube.");
    }
}

void ServiceManager::importTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Cannot open file: " + path);
        return;
    }

    QVariantList tracks;
    QSet<QString> seen;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || seen.contains(line)) continue;
        seen.insert(line);

        QVariantMap parsed = TrackMatch::fromQuery(line);
        TrackData track;
        track.artist = parsed["artist"].toString();
        track.title = parsed["title"].toString();
        // играется через поиск на ютубе, см. YouTubeService::resolveStreamUrl
        track.id = YouTubeService::queryTrackId(line);
        track.service = "YouTube";
        track.webUrl = "https://www.youtube.com/results?search_query=" + QUrl::toPercentEncoding(line);
        tracks.append(track.toVariantMap());
    }

    if (tracks.isEmpty()) {
        emit errorOccurred("No \"Artist - Title\" lines found in file");
        return;
    }
    emit playlistImported(QFileInfo(path).completeBaseName(), "", tracks);
}

void ServiceManager::setYandexToken(const QString& token) {
    bool changed = token != m_yandexToken;
    m_yandexToken = token;
    yandex->setToken(token);
    lyrics->setYandexToken(token);

    // QML зовёт это на каждом старте и перезагрузке стиля, лайки тянем только при новом токене
    if (changed && !token.isEmpty()) {
        yandex->fetchLikes();
        m_linkTimer->start();
    }
}

void ServiceManager::setSoundCloudClientId(const QString& clientId) {
    // QML зовёт это на каждом старте и перезагрузке стиля
    if (clientId == m_soundcloudToken) return;
    m_soundcloudToken = clientId;
    soundcloud->setToken(clientId);
    // свежий суточный токен: сверка с SoundCloud продолжается с того, на чём встала
    m_soundcloudWrites = 0;
    if (soundcloud->canWrite()) m_linkTimer->start();
}

void ServiceManager::setMixedWave(bool enabled) {
    mixer->setEnabled(enabled);
    // смешанная волна: треть мест за советами SoundCloud и YouTube, как у миксера
    m_recommender->setExternalShare(enabled ? 1.0 / 3 : 0);
}

void ServiceManager::setAudioQuality(const QString& quality) {
    setYandexQuality(quality);
}

void ServiceManager::setYandexQuality(const QString& quality) {
    yandex->setAudioQuality(quality);
}

void ServiceManager::setSoundCloudQuality(const QString& quality) {
    soundcloud->setAudioQuality(quality);
}
