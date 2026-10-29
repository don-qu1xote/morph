#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QVariantList>
#include "../network/NetworkManager.h"
#include "../utils/CacheManager.h"
#include "../services/YandexService.h"
#include "../services/SoundCloudService.h"
#include "../services/YouTubeService.h"

class ServiceManager : public QObject {
    Q_OBJECT
public:
    explicit ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent = nullptr);

public slots:
    void search(const QString& query, const QString& serviceName = "all");
    void getCharts();
    void getWave();
    void getDailyMixes();
    void resolve(const QString& serviceName, const QString& trackId);
    void reportPlay(const QString& serviceName, const QString& trackId, const QString& albumId);
    void reportListen(const QString& serviceName, const QString& trackId, const QString& albumId, double playedSeconds,
                      double lengthSeconds, bool completed);
    void setLiked(const QVariantMap& track, bool liked);
    void dislike(const QVariantMap& track);
    void importPlaylist(const QString& url);
    void setYandexToken(const QString& token);
    void setSoundCloudClientId(const QString& clientId);
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

private:
    void attach(BaseService* service);
    void importTextFile(const QString& path);
    void fallBackToSoundCloud(const QString& trackId);
    BaseService* service(const QString& name) const;

    QString m_yandexToken;
    // id трека, которого нет в Яндексе, и что по нему искать дальше по цепочке
    QHash<QString, QString> m_fallbackQueries;
    NetworkManager* net;
    CacheManager* cache;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    // имена такие же, как поле service у трека: "Yandex", "SoundCloud", "YouTube"
    QList<QPair<QString, BaseService*>> m_services;
};

#endif
