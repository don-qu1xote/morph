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
#include "../services/PinterestCovers.h"
#include "../services/LastFmApi.h"
#include "CandidateFilter.h"
#include "FallbackFinder.h"
#include "KnowledgeBase.h"
#include "MusicSpace.h"
#include "SoundLibrary.h"
#include "TasteRadio.h"
#include <QFutureWatcher>
#include <memory>
#include "Library.h"
#include "MorphMixes.h"
#include "PlaylistSync.h"
#include "Recommender.h"
#include "WaveMixer.h"

class QTimer;

class ServiceManager : public QObject {
    Q_OBJECT
public:
    explicit ServiceManager(NetworkManager* net, CacheManager* cache, QObject* parent = nullptr);
    // фильтр, ранжирование, обложки и прочее пишут файлы в фоне: дописать до выхода
    ~ServiceManager() override;

    Library* library() const { return m_library; }
    Recommender* recommender() const { return m_recommender; }
    // миксы для телефона: свой экземпляр, его готовность не открывает плейлист в окне
    MorphMixes* phoneMixes() const { return m_phoneMixes; }
    KnowledgeBase* knowledge() const { return m_knowledge; }
    // пространство музыки, собранное из того, что знает база; может быть пустым
    std::shared_ptr<const MusicSpace> space() const { return m_space; }
    // для настроек: сколько ответов Last.fm в базе и сколько артистов в пространстве
    Q_INVOKABLE QVariantMap tasteStatus() const;
    // сколько треков из кэша уже услышано и сколько ждут; decoder: есть ли ffmpeg
    Q_INVOKABLE QVariantMap soundStatus() const;

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
    // что добавить в плейлист name, ответ в playlistSuggestionsReady
    void playlistSuggestions(const QString& name, const QVariantList& tracks);
    // Поиск и сортировка внутри плейлиста. Все слова filter должны найтись в
    // названии, артисте или альбоме. sort: custom, title, artist, plays, duration
    QVariantList arrangeTracks(const QVariantList& tracks, const QString& filter, const QString& sort) const;
    void getDailyMixes();
    // недавно игравшее на всех площадках, для ряда на главной
    QVariantList recentTracks(int limit = 16) const { return m_recommender->history().recent(limit); }
    void resolve(const QString& serviceName, const QString& trackId);
    // Трек для телефона: ответ в phoneStreamReady или phoneStreamFailed. Окно
    // про него не слышит, иначе текущий трек в окне начался бы заново
    void fetchForPhone(const QString& serviceName, const QString& trackId);
    // прослушивание на телефоне учит рекомендации так же, как в окне
    void phoneListen(const QVariantMap& track, double playedSeconds, double lengthSeconds, bool completed);
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
    void setSyncToSoundCloud(bool enabled);
    // понравившееся в YouTube Music в MAIN, через cookies браузера; ответ в likesImported
    void importYouTubeLikes(const QString& browser);
    void setSyncPlaylists(bool enabled) { m_playlistSync->setEnabled(enabled); }
    // доска Pinterest для обложек: ссылка, имя или id
    void setPinterest(const QString& token, const QString& board) { m_covers->setAccount(token, board); }
    // ключ API Last.fm: на нём держится знание о похожем для своей модели
    void setLastFmKey(const QString& key);
    // новая обложка с доски для плейлиста key; пусто, если доска не загружена
    QString newCover(const QString& key) { return m_covers->next(key); }
    bool hasCovers() const { return m_covers->ready(); }
    bool isPinterestCover(const QString& url) const { return m_covers->isOurs(url); }
    // плейлисты morph, как их отдаёт MorphSettings.getPlaylists(): сверка с Яндексом
    void syncPlaylists(const QVariantMap& playlists) { m_playlistSync->sync(playlists); }
    void playlistRenamed(const QString& oldName, const QString& newName) { m_playlistSync->renamed(oldName, newName); }
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
    // лайки с площадки, куда morph не пишет: только добавить в MAIN
    void likesImported(const QString& serviceName, const QVariantList& tracks);
    // сообщение для интерфейса, не ошибка
    void notice(const QString& message);
    // доска Pinterest загрузилась: плейлистам можно раздавать новые обложки
    void coversReady();
    // трек играет с замены: service, id, artist, title, webUrl, index и total
    void fallbackChosen(const QString& trackId, const QVariantMap& source);
    void stationsReady(const QVariantList& stations);
    void waveSettingsReady(const QVariantList& options);
    void lyricsReady(const QString& trackId, const QVariantList& lines, bool synced, const QString& source);
    void lyricsMissing(const QString& trackId);
    // name, imageUrl, genres, listeners, popular, albums, similar и своё:
    // yours (песни из MAIN), inMain, plays; requestedName как в fetchArtist
    void artistReady(const QVariantMap& artist);
    // basis: contents (по трекам плейлиста), title (по названию) или taste (по вкусу)
    void playlistSuggestionsReady(const QString& name, const QVariantList& tracks, const QString& basis);
    void phoneStreamReady(const QString& trackId, const QString& streamUrl);
    void phoneStreamFailed(const QString& trackId);

private:
    void attach(BaseService* service);
    void importTextFile(const QString& path);
    static QVariantMap wantedFrom(const QVariantMap& track);
    BaseService* service(const QString& name) const;
    void directStreamFailed(const QString& trackId);
    void deliverStream(const QString& trackId, const QString& url);
    void linkNext();
    bool linkYandex();
    bool syncSoundCloud();
    QVariantMap knownTrack(const QString& trackId) const;
    void deliverArtist(const QString& requested, QVariantMap info);
    void emitMorphCards();

    QString m_yandexToken;
    QString m_soundcloudToken;
    // ищутся заранее и ещё не включены
    QSet<QString> m_prefetching;
    // включены напрямую с SoundCloud или YouTube, а не как замена
    QSet<QString> m_direct;
    // кто ждёт поток: окно, телефон или оба
    QSet<QString> m_windowWanted;
    QSet<QString> m_phoneWanted;
    QTimer* m_linkTimer;
    int m_linksThisRun = 0;
    bool m_syncToYandex = false;
    bool m_syncToSoundCloud = false;
    int m_soundcloudWrites = 0;
    int m_soundcloudSearches = 0;
    bool m_smartWaves = true;
    bool m_seededWave = false;
    Library* m_library;
    Recommender* m_recommender;
    MorphMixes* m_mixes;
    MorphMixes* m_phoneMixes;
    LastFmApi* m_lastfm;
    KnowledgeBase* m_knowledge;
    SoundLibrary* m_sounds;
    std::shared_ptr<const MusicSpace> m_space;
    TasteRadio* m_radio;
    QFutureWatcher<MusicSpace>* m_spaceBuild;
    QTimer* m_spaceTimer;
    QTimer* m_warmTimer;
    QTimer* m_warmRetry;
    // ключ как он сохранён в настройках, даже если он не того вида
    QString m_lastFmKey;
    // последняя сборка вышла пустой: похожих артистов пока слишком мало
    bool m_spaceEmpty = false;
    void rebuildSpace();
    void warmKnowledge();
    // что слушал: про песню и её артиста узнать у Last.fm, пространство растёт с прослушиванием
    void learnAbout(const QVariantMap& track);
    PlaylistSync* m_playlistSync;
    PinterestCovers* m_covers;
    // включённые и ещё не доигранные треки: прослушивание приходит только с id
    QHash<QString, QVariantMap> m_started;
    NetworkManager* net;
    CacheManager* cache;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    WaveMixer* mixer;
    CandidateFilter* m_filter;
    FallbackFinder* finder;
    LyricsService* lyrics;
    // имена такие же, как поле service у трека: "Yandex", "SoundCloud", "YouTube"
    QList<QPair<QString, BaseService*>> m_services;
};

#endif
