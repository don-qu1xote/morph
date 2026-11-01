#ifndef YANDEX_SERVICE_H
#define YANDEX_SERVICE_H

#include "BaseService.h"
#include "../network/NetworkManager.h"
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <functional>
#include <memory>

class YandexService : public BaseService {
    Q_OBJECT
public:
    explicit YandexService(NetworkManager* network, QObject* parent = nullptr);
    void setToken(const QString& token);
    void setAudioQuality(const QString& quality);
    void search(const QString& query) override;
    void resolveStreamUrl(const QString& trackId) override;
    void getCharts() override;
    void getWave() override;
    void getDailyMixes() override;
    void reportPlay(const QString& trackId, const QString& albumId) override;
    void importPlaylist(const QString& url) override;

    void reportListen(const QString& trackId, double playedSeconds, double lengthSeconds, bool completed);
    // поток трека yandexId под чужим id trackId; не вышло, значит streamFailed
    void streamAs(const QString& yandexId, const QString& trackId);
    // треки по запросу; при ошибке пустой список, ответ приходит всегда
    void searchTracks(const QString& text, std::function<void(const QVariantList&)> found);
    // похожие на трек, как блок «Похожие треки» в приложении Яндекса
    void similarTracks(const QString& trackId, std::function<void(const QVariantList&)> found);
    // последние альбомы артиста, новые первыми
    void artistAlbums(const QString& artistId, std::function<void(const QVariantList&)> found);
    void albumTracks(const QString& albumId, std::function<void(const QVariantList&)> found);
    static QVariantMap parseAlbum(const QJsonObject& album);
    void setLiked(const QString& trackId, bool liked);
    void dislike(const QString& trackId);
    void fetchLikes();

    // волна по сидам: user:onyourwave, track:ID, artist:ID, genre:rap и т.п.
    void startWave(const QStringList& seeds);
    void startTrackWave(const QVariantMap& track);
    void startArtistWave(const QString& artistName);
    // настройки Моей волны сидами вида settingDiversity:discover
    void setWaveSettings(const QStringList& settingSeeds);
    void fetchWaveStations();
    // false для треков, которые Яндекс знает, но больше не отдаёт
    bool isAvailable(const QString& trackId) const { return !m_unavailable.contains(trackId); }

signals:
    // allIds это все лайкнутые id, даже те, по которым /tracks не отдал данных
    void likesFetched(const QVariantList& tracks, const QStringList& allIds);
    // трек есть в базе, но не отдаётся (убран из каталога); длительность 0, если неизвестна
    void streamUnavailable(const QString& trackId, const QString& artist, const QString& title, qint64 durationMs);
    void streamFailed(const QString& trackId);
    // name, seed, imageUrl, color
    void stationsReady(const QVariantList& stations);
    // key, name и values: name, seed, unspecified
    void waveSettingsReady(const QVariantList& options);

private:
    // refused: сервер ответил, что трек не отдаст; иначе это сеть
    using StreamFailure = std::function<void(bool refused, const QString& error)>;
    void requestStream(const QString& trackId, const QString& target, StreamFailure failed);
    void fetchDownloadInfo(const QString& trackId, const QUrl& url, int bitrate, StreamFailure failed);
    QVariantList parseYandexTracks(const QJsonArray& tracksArray);
    QString fullTrackId(const QString& trackId) const;
    void reportUnavailable(const QString& trackId);
    void withUid(std::function<void()> next);
    void fetchTracksChunk(const QStringList& fullIds, const QStringList& allIds, int from,
                          std::shared_ptr<QVariantList> result);

    void startWaveSession();
    void resetWaveSession();
    QStringList sessionSeeds() const;
    void findFirst(const QString& type, const QString& text, std::function<void(const QString&)> found);
    // GET по адресу, треки из result[field]
    void getTracks(const QUrl& url, const QString& field, std::function<void(const QVariantList&)> found);
    void fetchDashboard(QVariantList stations);
    void handleWaveBatch(const QJsonObject& result);
    void getWaveFromStation();
    void sendWaveFeedback(const QString& type, const QString& trackId = QString(), double playedSeconds = -1);

private:
    NetworkManager* net;
    QString m_api;
    QString m_token;
    QString m_quality = "320";
    QString m_uid;
    QList<std::function<void()>> m_uidWaiters;

    // в QML в поле album уходит название, а API нужен id альбома, поэтому
    // id и длительность запоминаем, когда трек проходит через parseYandexTracks
    QHash<QString, QString> m_albumIds;
    QHash<QString, qint64> m_durations;
    struct Names {
        QString artist;
        QString title;
    };
    QHash<QString, Names> m_names;
    QSet<QString> m_unavailable;

    QString m_waveSession;
    QString m_waveBatch;
    QHash<QString, QString> m_waveBatchOf;
    QStringList m_waveQueue;
    bool m_waveLoading = false;
    int m_waveGeneration = 0;
    QStringList m_waveSeeds = {"user:onyourwave"};
    QStringList m_waveSettingSeeds;
};

#endif
