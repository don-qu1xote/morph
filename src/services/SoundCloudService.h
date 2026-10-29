#ifndef SOUNDCLOUD_SERVICE_H
#define SOUNDCLOUD_SERVICE_H

#include "BaseService.h"
#include "../network/NetworkManager.h"

#include <QDateTime>
#include <QMap>
#include <QUrlQuery>

class SoundCloudService : public BaseService {
    Q_OBJECT
public:
    explicit SoundCloudService(NetworkManager* network, QObject* parent = nullptr);
    void setToken(const QString& token);
    void setAudioQuality(const QString& quality);
    void search(const QString& query) override;
    void resolveStreamUrl(const QString& trackId) override;
    void getCharts() override;
    void getWave() override;
    void getDailyMixes() override;
    void reportPlay(const QString& trackId, const QString& albumId) override;
    void importPlaylist(const QString& url) override;

    // найти по "артист - название" и отдать поток под чужим id
    void resolveByQuery(const QString& query, const QString& trackId);
    // похожее на трек из запроса, ответ в relatedReady
    void relatedTo(const QString& query, int count);
    bool hasToken() const { return !m_token.isEmpty(); }

signals:
    void notFound(const QString& trackId, const QString& query);
    // пустой список, если ничего не нашлось или запрос упал
    void relatedReady(const QVariantList& tracks);

private:
    void fetchStreamUrl(const QString& trackId, const QString& transcodingUrl, int bitrate);
    void resolveAs(const QString& soundcloudId, const QString& trackId);
    void addClientId(QUrlQuery& query) const;
    QMap<QString, QByteArray> authHeaders() const;
    void fetchPlaylistTracksMetadata(const QString& playlistName, const QString& coverUrl, const QStringList& trackIds);
    void fetchNextPlaylistChunk(const QString& playlistName, const QString& coverUrl, QStringList* remainingIds, QVariantList* allTracks, const QStringList& originalIds);
    QVariantList parseSoundCloudTracks(const QJsonArray& tracks);

private:
    NetworkManager* net;
    QString m_api;
    QString m_token;
    QString m_clientId;
    QString m_oauth;
    QDateTime m_oauthExpires;
    QString m_quality = "128";
    QMap<QString, QString> m_trackLinks;
};

#endif
