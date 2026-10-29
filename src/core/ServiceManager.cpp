#include "ServiceManager.h"
#include "../services/TrackData.h"
#include "TrackMatch.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

ServiceManager::ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent)
    : QObject(parent), net(net), cache(cache) {
    connect(net, &NetworkManager::errorOccurred, this, &ServiceManager::errorOccurred);
    yandex = new YandexService(net, this);
    soundcloud = new SoundCloudService(net, this);
    youtube = new YouTubeService(this);
    mixer = new WaveMixer(soundcloud, youtube, this);
    connect(mixer, &WaveMixer::waveReady, this, &ServiceManager::waveReady);

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
    connect(this, &ServiceManager::streamUrlReady, this, [this](const QString& trackId, const QString& url) {
        // поток с YouTube через VPN начинает играть заметно позже яндексовского,
        // поэтому следующий трек сразу ложится в кэш и потом стартует с диска
        if (m_prefetching.remove(trackId) && url.contains("googlevideo.com") && this->cache->savesTracks()) {
            this->cache->cacheTrack(trackId, url);
        }
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
    for (const auto& [name, s] : m_services) s->getWave();
}

void ServiceManager::getDailyMixes() {
    for (const auto& [name, s] : m_services) s->getDailyMixes();
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
    if (s) s->resolveStreamUrl(trackId);
}

// Следующий трек ищется, пока играет текущий. Яндекс и SoundCloud отвечают
// одним быстрым запросом, а yt-dlp встаёт в очередь за тем, что уже включено
void ServiceManager::prefetch(const QString& serviceName, const QString& trackId) {
    BaseService* s = service(serviceName);
    if (!s || trackId.isEmpty() || cache->isTrackCached(trackId) || s->isTrackCached(trackId)) return;

    m_prefetching.insert(trackId);
    QString query = YouTubeService::queryFromTrackId(trackId);
    if (s == youtube && !query.isEmpty()) finder->find(trackId, TrackMatch::fromQuery(query), true);
    else if (s == youtube) youtube->prefetch(trackId);
    else s->resolveStreamUrl(trackId);
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

void ServiceManager::reportListen(const QString& serviceName, const QString& trackId, const QString& albumId,
                                  double playedSeconds, double lengthSeconds, bool completed) {
    Q_UNUSED(albumId);
    if (completed) mixer->trackFinished(trackId);
    if (serviceName == "Yandex") yandex->reportListen(trackId, playedSeconds, lengthSeconds, completed);
}

void ServiceManager::setLiked(const QVariantMap& track, bool liked) {
    if (liked) mixer->addSeed(track);
    if (track["service"].toString() == "Yandex") yandex->setLiked(track["id"].toString(), liked);
}

void ServiceManager::dislike(const QVariantMap& track) {
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

    if (url.contains("yandex")) {
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

    // QML зовёт это на каждом старте и перезагрузке стиля, лайки тянем только при новом токене
    if (changed && !token.isEmpty()) yandex->fetchLikes();
}

void ServiceManager::setSoundCloudClientId(const QString& clientId) {
    soundcloud->setToken(clientId);
}

void ServiceManager::setMixedWave(bool enabled) {
    mixer->setEnabled(enabled);
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
