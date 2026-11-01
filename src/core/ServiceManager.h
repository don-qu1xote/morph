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
#include "../services/LyricsService.h"
#include "FallbackFinder.h"
#include "Library.h"
#include "MorphMixes.h"
#include "Recommender.h"
#include "WaveMixer.h"

class QTimer;

class ServiceManager : public QObject {
    Q_OBJECT
public:
    explicit ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent = nullptr);

    Library* library() const { return m_library; }
    Recommender* recommender() const { return m_recommender; }

public slots:
    void search(const QString& query, const QString& serviceName = "all");
    void getCharts();
    void getWave();
    void startWave(const QStringList& seeds);
    void startTrackWave(const QVariantMap& track);
    void startArtistWave(const QString& artistName);
    void setWaveSettings(const QStringList& settingSeeds);
    void fetchWaveStations();
    void fetchLyrics(const QVariantMap& track);
    // страница артиста: из Яндекса, если он там есть, иначе треки с SoundCloud
    void fetchArtist(const QString& name, const QVariantMap& track);
    // альбом со страницы артиста открывается как плейлист
    void openAlbum(const QVariantMap& album);
    // волна по трекам плейлиста; дальше getWave просит её же, пока не начнётся другая
    void startPlaylistWave(const QVariantList& tracks);
    // Поиск и сортировка внутри плейлиста. Все слова filter должны найтись в
    // названии, артисте или альбоме. sort: custom, title, artist, plays, duration
    QVariantList arrangeTracks(const QVariantList& tracks, const QString& filter, const QString& sort) const;
    void getDailyMixes();
    void resolve(const QString& serviceName, const QString& trackId);
    void prefetch(const QString& serviceName, const QString& trackId);
    void otherVersion(const QVariantMap& track);
    QVariantMap fallbackSource(const QVariantMap& track) const;
    void reportPlay(const QString& serviceName, const QString& trackId, const QString& albumId);
    // то же, что reportPlay, но с самим треком: истории нужны артист и название
    void nowPlaying(const QVariantMap& track);
    void reportListen(const QString& serviceName, const QString& trackId, const QString& albumId, double playedSeconds,
                      double lengthSeconds, bool completed);
    void setLiked(const QVariantMap& track, bool liked);
    void dislike(const QVariantMap& track);
    void importPlaylist(const QString& url);
    void setYandexToken(const QString& token);
    void setSoundCloudClientId(const QString& clientId);
    void setMixedWave(bool enabled);
    void setSyncToYandex(bool enabled) { m_syncToYandex = enabled; }
    void setSmartWaves(bool enabled) { m_smartWaves = enabled; }
    // familiar, balanced или new: сколько незнакомых артистов в волнах и миксах
    void setDiscovery(const QString& level);
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
    void lyricsReady(const QString& trackId, const QVariantList& lines, bool synced, const QString& source);
    void lyricsMissing(const QString& trackId);
    // name, imageUrl, genres, listeners, popular, albums, similar и своё:
    // yours (песни из MAIN), inMain, plays; requestedName как в fetchArtist
    void artistReady(const QVariantMap& artist);

private:
    void attach(BaseService* service);
    void importTextFile(const QString& path);
    static QVariantMap wantedFrom(const QVariantMap& track);
    BaseService* service(const QString& name) const;
    void directStreamFailed(const QString& trackId);
    void linkNext();
    QVariantMap knownTrack(const QString& trackId) const;
    void deliverArtist(const QString& requested, QVariantMap info);

    QString m_yandexToken;
    // ищутся заранее и ещё не включены
    QSet<QString> m_prefetching;
    // включены напрямую с SoundCloud или YouTube, а не как замена
    QSet<QString> m_direct;
    QTimer* m_linkTimer;
    int m_linksThisRun = 0;
    bool m_syncToYandex = false;
    bool m_smartWaves = true;
    bool m_seededWave = false;
    Library* m_library;
    Recommender* m_recommender;
    MorphMixes* m_mixes;
    // включённые и ещё не доигранные треки: прослушивание приходит только с id
    QHash<QString, QVariantMap> m_started;
    NetworkManager* net;
    CacheManager* cache;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    WaveMixer* mixer;
    FallbackFinder* finder;
    LyricsService* lyrics;
    // имена такие же, как поле service у трека: "Yandex", "SoundCloud", "YouTube"
    QList<QPair<QString, BaseService*>> m_services;
};

#endif
