#include "ServiceManager.h"
#include "../services/TrackData.h"
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

    m_services = {{"Yandex", yandex}, {"SoundCloud", soundcloud}, {"YouTube", youtube}};
    for (const auto& [name, s] : m_services) attach(s);

    // трек убрали из Яндекса: та же песня с ютуба, но под яндексовским id,
    // так QML ничего не замечает, а кэш потом отдаёт её уже офлайн
    connect(yandex, &YandexService::streamUnavailable, this, [this](const QString& trackId, const QString& query) {
        m_fallbackQueries[trackId] = query;
        youtube->resolveByQuery(query, trackId);
    });
    connect(youtube, &YouTubeService::resolveFailed, this, &ServiceManager::fallBackToSoundCloud);
    connect(soundcloud, &SoundCloudService::notFound, this, [this](const QString& trackId, const QString& query) {
        m_fallbackQueries.remove(trackId);
        emit errorOccurred("Not found on YouTube or SoundCloud: " + query);
    });
    connect(this, &ServiceManager::streamUrlReady, this,
            [this](const QString& trackId) { m_fallbackQueries.remove(trackId); });

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

// на ютубе не нашлось: пробуем SoundCloud. Касается только треков из цепочки
// фолбэка и импортированных списков, обычный ютуб-трек сюда не уходит
void ServiceManager::fallBackToSoundCloud(const QString& trackId) {
    QString query = m_fallbackQueries.value(trackId, YouTubeService::queryFromTrackId(trackId));
    if (query.isEmpty()) return;
    soundcloud->resolveByQuery(query, trackId);
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

        TrackData track;
        track.title = line;
        int sep = line.indexOf(" - ");
        if (sep > 0) {
            track.artist = line.left(sep);
            track.title = line.mid(sep + 3);
        }
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

void ServiceManager::setAudioQuality(const QString& quality) {
    setYandexQuality(quality);
}

void ServiceManager::setYandexQuality(const QString& quality) {
    yandex->setAudioQuality(quality);
}

void ServiceManager::setSoundCloudQuality(const QString& quality) {
    soundcloud->setAudioQuality(quality);
}
