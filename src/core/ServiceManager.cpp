#include "ServiceManager.h"

ServiceManager::ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent)
    : QObject(parent), net(net), cache(cache) {
    connect(net, &NetworkManager::errorOccurred, this, &ServiceManager::errorOccurred);
    yandex = new YandexService(net, this);
    soundcloud = new SoundCloudService(net, this);
    youtube = new YouTubeService(this);

    m_services = {{"Yandex", yandex}, {"SoundCloud", soundcloud}, {"YouTube", youtube}};
    for (const auto& [name, s] : m_services) attach(s);

    connect(yandex, &YandexService::likesFetched, this, [this](const QVariantList& tracks, const QStringList& allIds) {
        emit likesFetched("Yandex", tracks, allIds);
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
    connect(s, &BaseService::waveReady, this, [this, withDuration](const QString& name, const QVariantList& r) {
        emit waveReady(name, withDuration(r));
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
    BaseService* s = service(serviceName);
    if (cache->isTrackCached(trackId) || (s && s->isTrackCached(trackId))) {
        emit streamUrlReady(trackId, cache->getTrackUrl(trackId));
        return;
    }
    if (s) s->resolveStreamUrl(trackId);
}

void ServiceManager::reportPlay(const QString& serviceName, const QString& trackId, const QString& albumId) {
    emit trackStarted(serviceName, trackId, albumId);
    if (BaseService* s = service(serviceName)) s->reportPlay(trackId, albumId);
}

void ServiceManager::reportListen(const QString& serviceName, const QString& trackId, const QString& albumId,
                                  double playedSeconds, double lengthSeconds, bool completed) {
    Q_UNUSED(albumId);
    if (serviceName == "Yandex") yandex->reportListen(trackId, playedSeconds, lengthSeconds, completed);
}

void ServiceManager::setLiked(const QVariantMap& track, bool liked) {
    if (track["service"].toString() == "Yandex") yandex->setLiked(track["id"].toString(), liked);
}

void ServiceManager::importPlaylist(const QString& url) {
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

void ServiceManager::setAudioQuality(const QString& quality) {
    setYandexQuality(quality);
}

void ServiceManager::setYandexQuality(const QString& quality) {
    yandex->setAudioQuality(quality);
}

void ServiceManager::setSoundCloudQuality(const QString& quality) {
    soundcloud->setAudioQuality(quality);
}
