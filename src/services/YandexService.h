#ifndef YANDEX_SERVICE_H
#define YANDEX_SERVICE_H

#include "BaseService.h"
#include "../network/NetworkManager.h"
#include <QHash>
#include <QStringList>

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

private:
    void fetchDownloadInfo(const QString& trackId, const QUrl& url, int bitrate);
    QVariantList parseYandexTracks(const QJsonArray& tracksArray);
    QString fullTrackId(const QString& trackId) const;

    void startWaveSession();
    void handleWaveBatch(const QJsonObject& result);
    void getWaveFromStation();
    void sendWaveFeedback(const QString& type, const QString& trackId = QString(), double playedSeconds = -1);

private:
    NetworkManager* net;
    QString m_api;
    QString m_token;
    QString m_quality = "320";

    // в QML в поле album уходит название, а API нужен id альбома, поэтому
    // id и длительность запоминаем, когда трек проходит через parseYandexTracks
    QHash<QString, QString> m_albumIds;
    QHash<QString, qint64> m_durations;

    QString m_waveSession;
    QString m_waveBatch;
    QHash<QString, QString> m_waveBatchOf;
    QStringList m_waveQueue;
    bool m_waveLoading = false;
};

#endif
