#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariantList>
#include "../network/NetworkManager.h"
#include "../utils/CacheManager.h"
#include "../services/YandexService.h"
#include "../services/SoundCloudService.h"
#include "../services/YouTubeService.h"
#include "FallbackFinder.h"
#include "WaveMixer.h"

class ServiceManager : public QObject {
    Q_OBJECT
public:
    explicit ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent = nullptr);

public slots:
    void search(const QString& query, const QString& serviceName = "all");
    void getCharts();
    void getWave();
    void startWave(const QStringList& seeds);
    void startTrackWave(const QVariantMap& track);
    void startArtistWave(const QString& artistName);
    void setWaveSettings(const QStringList& settingSeeds);
    void fetchWaveStations();
    void getDailyMixes();
    void resolve(const QString& serviceName, const QString& trackId);
    void prefetch(const QString& serviceName, const QString& trackId);
    void otherVersion(const QVariantMap& track);
    QVariantMap fallbackSource(const QVariantMap& track) const;
    void reportPlay(const QString& serviceName, const QString& trackId, const QString& albumId);
    void reportListen(const QString& serviceName, const QString& trackId, const QString& albumId, double playedSeconds,
                      double lengthSeconds, bool completed);
    void setLiked(const QVariantMap& track, bool liked);
    void dislike(const QVariantMap& track);
    void importPlaylist(const QString& url);
    void setYandexToken(const QString& token);
    void setSoundCloudClientId(const QString& clientId);
    void setMixedWave(bool enabled);
    void setAudioQuality(const QString& quality);
    void setYandexQuality(const QString& quality);
    void setSoundCloudQuality(const QString& quality);

signals:
    void searchResultsReady(const QString& serviceName, const QVariantList& results);
    void streamUrlReady(const QString& trackId, const QString& streamUrl);
    void chartsReady(const QString& serviceName, const QVariantList& tracks);
    void waveReady(const QString& serviceName, const QVariantList& tracks);
    void dailyMixesReady(const QString& serviceName, const QVariantList& playlists);
    void playlistImported(const QString& name, const QString& coverUrl, const QVariantList& tracks);
    void errorOccurred(const QString& message);
    void bitrateReady(const QString& trackId, int bitrate);
    void trackStarted(const QString& serviceName, const QString& trackId, const QString& albumId);
    void likesFetched(const QString& serviceName, const QVariantList& tracks, const QStringList& allIds);
    // трек играет с замены: service, id, artist, title, webUrl, index и total
    void fallbackChosen(const QString& trackId, const QVariantMap& source);
    void stationsReady(const QVariantList& stations);
    void waveSettingsReady(const QVariantList& options);

private:
    void attach(BaseService* service);
    void importTextFile(const QString& path);
    static QVariantMap wantedFrom(const QVariantMap& track);
    BaseService* service(const QString& name) const;

    QString m_yandexToken;
    // ищутся заранее и ещё не включены
    QSet<QString> m_prefetching;
    NetworkManager* net;
    CacheManager* cache;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    WaveMixer* mixer;
    FallbackFinder* finder;
    // имена такие же, как поле service у трека: "Yandex", "SoundCloud", "YouTube"
    QList<QPair<QString, BaseService*>> m_services;
};

#endif
