#include "SoundCloudService.h"
#include "TrackData.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>

SoundCloudService::SoundCloudService(NetworkManager* network, QObject* parent)
    : BaseService(parent), net(network),
      m_api(qEnvironmentVariable("MORPH_SOUNDCLOUD_API", "https://api-v2.soundcloud.com")) {}

// В поле настроек может оказаться одно из трёх:
//  - client_id веб-клиента из любого запроса api-v2: поиск и потоки работают;
//  - старый OAuth вида 2-...: работает всё, включая Daily Mixes;
//  - нынешний JWT eyJ... из заголовка Authorization. Он живёт сутки, поэтому
//    client_id достаём из него же, а сам токен шлём, только пока он не истёк
void SoundCloudService::setToken(const QString& token) {
    m_token = token.trimmed();
    if (m_token.startsWith("OAuth ")) m_token = m_token.mid(6).trimmed();

    m_clientId.clear();
    m_oauth.clear();
    m_oauthExpires = QDateTime();

    if (m_token.startsWith("eyJ")) {
        QByteArray payload = m_token.section('.', 1, 1).toLatin1();
        QJsonObject claims =
            QJsonDocument::fromJson(QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding)).object();
        m_clientId = claims["client_id"].toString();
        m_oauth = m_token;
        m_oauthExpires = QDateTime::fromSecsSinceEpoch(claims["exp"].toInteger());
    } else if (m_token.startsWith("2-")) {
        m_oauth = m_token;
    } else {
        m_clientId = m_token;
    }
}

void SoundCloudService::setAudioQuality(const QString& quality) {
    if (m_quality != quality) {
        m_quality = quality;
        m_trackLinks.clear();
    }
}

void SoundCloudService::addClientId(QUrlQuery& query) const {
    if (!m_clientId.isEmpty()) query.addQueryItem("client_id", m_clientId);
}

QMap<QString, QByteArray> SoundCloudService::authHeaders() const {
    QMap<QString, QByteArray> h;
    bool expired = m_oauthExpires.isValid() && QDateTime::currentDateTimeUtc() >= m_oauthExpires;
    if (!m_oauth.isEmpty() && !expired) h["Authorization"] = "OAuth " + m_oauth.toUtf8();
    return h;
}

QVariantList SoundCloudService::parseSoundCloudTracks(const QJsonArray& tracks) {
    QVariantList results;
    for (const QJsonValue& value : tracks) {
        QJsonObject obj = value.toObject();
        TrackData track;
        track.id = obj["id"].toVariant().toString();
        track.title = obj["title"].toString();
        track.durationMs = obj["duration"].toVariant().toLongLong();

        QJsonObject pub = obj["publisher_metadata"].toObject();
        if (!pub.isEmpty() && !pub["artist"].toString().isEmpty()) {
            track.artist = pub["artist"].toString();
        } else {
            track.artist = obj["user"].toObject()["username"].toString();
        }

        track.coverUrl = obj["artwork_url"].toString().replace("-large", "-t500x500");
        track.webUrl = obj["permalink_url"].toString();

        track.service = "SoundCloud";
        results.append(track.toVariantMap());
    }
    return results;
}

void SoundCloudService::search(const QString& query) {
    if (m_token.isEmpty()) return;

    if (query.startsWith("http") && query.contains("soundcloud.com")) {
        QUrl url(m_api + "/resolve");
        QUrlQuery q;
        q.addQueryItem("url", query);
        addClientId(q);
        url.setQuery(q);

        net->get(url, authHeaders(), [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) return;
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject result = doc.object();
            if (result["kind"].toString() == "track") {
                QJsonArray tracks;
                tracks.append(result);
                emit searchResultsReady("SoundCloud", parseSoundCloudTracks(tracks));
            }
        });
        return;
    }

    QUrl url(m_api + "/search/tracks");
    QUrlQuery q;
    q.addQueryItem("q", query);
    addClientId(q);
    q.addQueryItem("limit", "100");
    url.setQuery(q);

    net->get(url, authHeaders(), [this](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray tracks = doc.object()["collection"].toArray();
        m_trackLinks.clear();
        emit searchResultsReady("SoundCloud", parseSoundCloudTracks(tracks));
    });
}

void SoundCloudService::getCharts() {
    emit chartsReady("SoundCloud", QVariantList());
}

void SoundCloudService::getWave() {
    emit waveReady("SoundCloud", QVariantList());
}

void SoundCloudService::getDailyMixes() {
    if (m_token.isEmpty()) return;

    QUrl url(m_api + "/mixed-selections");
    QUrlQuery q;
    addClientId(q);
    url.setQuery(q);

    net->get(url, authHeaders(), [this](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray collection = doc.object()["collection"].toArray();

        QVariantList mixedPlaylists;

        auto processSection = [&](const QJsonObject& section) {
            QJsonArray items = section["items"].toObject()["collection"].toArray();
            for (const QJsonValue& itemValue : items) {
                QJsonObject item = itemValue.toObject();
                QString kind = item["kind"].toString();
                if (kind == "playlist" || kind == "system-playlist") {
                    QVariantMap pl;
                    pl["id"] = item["id"].toVariant().toString();
                    pl["name"] = item["title"].toString();
                    pl["coverUrl"] = item["artwork_url"].toString().replace("-large", "-t500x500");
                    if (pl["coverUrl"].toString().isEmpty()) {
                        pl["coverUrl"] = item["calculated_artwork_url"].toString().replace("-large", "-t500x500");
                    }
                    pl["service"] = "SoundCloud";
                    pl["permalink_url"] = item["permalink_url"].toString();
                    mixedPlaylists.append(pl);
                }
            }
        };

        for (const QJsonValue& sectionValue : collection) {
            QJsonObject section = sectionValue.toObject();
            QString title = section["title"].toString().toLower();
            if (title.startsWith("mixed for ")) {
                processSection(section);
            }
        }

        if (mixedPlaylists.size() < 10) {
            for (const QJsonValue& sectionValue : collection) {
                QJsonObject section = sectionValue.toObject();
                if (section["title"].toString().toLower() == "made for you") {
                    processSection(section);
                }
            }
        }

        if (mixedPlaylists.isEmpty()) {
            for (const QJsonValue& sectionValue : collection) {
                QJsonObject section = sectionValue.toObject();
                QString title = section["title"].toString().toLower();
                if (title.contains("mixed") || title.contains("made for you")) {
                    processSection(section);
                }
                if (mixedPlaylists.size() >= 10) break;
            }
        }

        emit dailyMixesReady("SoundCloud", mixedPlaylists.mid(0, 12));
    });
}

void SoundCloudService::reportPlay(const QString& trackId, const QString& albumId) {
    Q_UNUSED(trackId);
    Q_UNUSED(albumId);
}

void SoundCloudService::importPlaylist(const QString& url) {
    if (m_token.isEmpty()) return;

    if (url.contains("on.soundcloud.com")) {
        net->get(QUrl(url), QMap<QString, QByteArray>(), [this](QNetworkReply* reply) {
            QUrl redirectedUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (redirectedUrl.isValid()) {
                QString finalUrl = reply->url().resolved(redirectedUrl).toString();
                importPlaylist(finalUrl);
            } else {
                importPlaylist(reply->url().toString());
            }
        });
        return;
    }

    QUrl apiUrl(m_api + "/resolve");
    QUrlQuery q;
    q.addQueryItem("url", url);
    addClientId(q);
    apiUrl.setQuery(q);

    net->get(apiUrl, authHeaders(), [this](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("SoundCloud API Error: " + reply->errorString());
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject result = doc.object();
        QString kind = result["kind"].toString();

        if (kind != "playlist" && kind != "system-playlist") {
            emit errorOccurred("Invalid SoundCloud URL or not a playlist");
            return;
        }

        QString name = result["title"].toString();
        QString coverUrl = result["artwork_url"].toString().replace("-large", "-t500x500");
        if (coverUrl.isEmpty()) {
            coverUrl = result["calculated_artwork_url"].toString().replace("-large", "-t500x500");
        }
        QJsonArray tracks = result["tracks"].toArray();
        if (coverUrl.isEmpty() && !tracks.isEmpty()) {
            QJsonObject firstTrack = tracks.first().toObject();
            coverUrl = firstTrack["artwork_url"].toString().replace("-large", "-t500x500");
        }

        QStringList allTrackIds;
        for (const QJsonValue& val : tracks) {
            QJsonObject tObj = val.toObject();
            QString tid;
            if (tObj.contains("id")) tid = tObj["id"].toVariant().toString();
            else tid = val.toVariant().toString();

            if (!tid.isEmpty()) allTrackIds.append(tid);
        }

        if (allTrackIds.isEmpty()) {
            emit errorOccurred("Playlist is empty");
            return;
        }

        fetchPlaylistTracksMetadata(name, coverUrl, allTrackIds);
    });
}

void SoundCloudService::fetchPlaylistTracksMetadata(const QString& playlistName, const QString& coverUrl, const QStringList& trackIds) {
    auto remainingIds = new QStringList(trackIds);
    auto allTracks = new QVariantList();
    fetchNextPlaylistChunk(playlistName, coverUrl, remainingIds, allTracks, trackIds);
}

void SoundCloudService::fetchNextPlaylistChunk(const QString& playlistName, const QString& coverUrl, QStringList* remainingIds, QVariantList* allTracks, const QStringList& originalIds) {
    if (remainingIds->isEmpty()) {
        QVariantList sortedTracks;
        QMap<QString, QVariantMap> trackMap;
        for (const QVariant& tVal : *allTracks) {
            QVariantMap tMap = tVal.toMap();
            trackMap[tMap["id"].toString()] = tMap;
        }
        for (const QString& id : originalIds) {
            if (trackMap.contains(id)) {
                sortedTracks.append(trackMap[id]);
            }
        }
        QString finalCoverUrl = coverUrl;
        if (finalCoverUrl.isEmpty() && !sortedTracks.isEmpty()) {
            finalCoverUrl = sortedTracks.first().toMap()["coverUrl"].toString();
        }
        emit playlistImported(playlistName, finalCoverUrl, sortedTracks);
        delete remainingIds;
        delete allTracks;
        return;
    }

    int chunkSize = 50;
    QStringList currentChunk;
    for (int i = 0; i < chunkSize && !remainingIds->isEmpty(); ++i) {
        currentChunk.append(remainingIds->takeFirst());
    }

    QUrl url(m_api + "/tracks");
    QUrlQuery q;
    q.addQueryItem("ids", currentChunk.join(","));
    addClientId(q);
    url.setQuery(q);

    net->get(url, authHeaders(),
             [this, playlistName, coverUrl, remainingIds, allTracks, originalIds](QNetworkReply* reply) {
                 if (reply->error() == QNetworkReply::NoError) {
                     QByteArray data = reply->readAll();
                     QJsonArray results = QJsonDocument::fromJson(data).array();
                     for (const auto& val : results) {
                         QJsonObject obj = val.toObject();
                         TrackData track;
                         track.id = obj["id"].toVariant().toString();
                         track.title = obj["title"].toString();
                         track.durationMs = obj["duration"].toVariant().toLongLong();

                         QJsonObject pub = obj["publisher_metadata"].toObject();
                         if (!pub.isEmpty() && !pub["artist"].toString().isEmpty()) {
                             track.artist = pub["artist"].toString();
                         } else {
                             track.artist = obj["user"].toObject()["username"].toString();
                         }

                         track.coverUrl = obj["artwork_url"].toString().replace("-large", "-t500x500");
                         track.webUrl = obj["permalink_url"].toString();
                         track.service = "SoundCloud";
                         allTracks->append(track.toVariantMap());
                     }
                 }
                 fetchNextPlaylistChunk(playlistName, coverUrl, remainingIds, allTracks, originalIds);
             });
}

void SoundCloudService::resolveStreamUrl(const QString& trackId) {
    if (m_token.isEmpty()) return;
    streamAs(trackId, trackId);
}

// Варианты замены для трека, которого нет в Яндексе: выбирает FallbackFinder
void SoundCloudService::searchCandidates(const QString& query, const QString& trackId) {
    if (m_token.isEmpty()) {
        emit candidatesReady(trackId, {});
        return;
    }

    QUrl url(m_api + "/search/tracks");
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("limit", "10");
    addClientId(q);
    url.setQuery(q);

    net->get(
        url, authHeaders(),
        [this, trackId](QNetworkReply* reply) {
            QJsonArray found = QJsonDocument::fromJson(reply->readAll()).object()["collection"].toArray();
            if (reply->error() != QNetworkReply::NoError) found = {};
            emit candidatesReady(trackId, parseSoundCloudTracks(playableOnly(found)));
        },
        false);
}

// Для смешанной волны: находим трек по запросу и берём его related, как блок
// «Related tracks» на сайте
void SoundCloudService::relatedTo(const QString& query, int count) {
    if (m_token.isEmpty()) {
        emit relatedReady({});
        return;
    }

    QUrl url(m_api + "/search/tracks");
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("limit", "1");
    addClientId(q);
    url.setQuery(q);

    net->get(
        url, authHeaders(),
        [this, count](QNetworkReply* reply) {
            QJsonArray found = QJsonDocument::fromJson(reply->readAll()).object()["collection"].toArray();
            if (reply->error() != QNetworkReply::NoError || found.isEmpty()) {
                emit relatedReady({});
                return;
            }

            QString id = found[0].toObject()["id"].toVariant().toString();
            QUrl relatedUrl(QString("%1/tracks/%2/related").arg(m_api, id));
            QUrlQuery rq;
            rq.addQueryItem("limit", QString::number(count));
            addClientId(rq);
            relatedUrl.setQuery(rq);

            net->get(
                relatedUrl, authHeaders(),
                [this](QNetworkReply* reply) {
                    QJsonArray collection = QJsonDocument::fromJson(reply->readAll()).object()["collection"].toArray();
                    emit relatedReady(parseSoundCloudTracks(playableOnly(collection)));
                },
                false);
        },
        false);
}

// SNIP это 30 секунд превью для Go+, BLOCK не играет вовсе
QJsonArray SoundCloudService::playableOnly(const QJsonArray& tracks) {
    QJsonArray playable;
    for (const auto& t : tracks) {
        QString policy = t.toObject()["policy"].toString();
        if (policy != "SNIP" && policy != "BLOCK") playable.append(t);
    }
    return playable;
}

// soundcloudId это что запрашиваем, trackId это под каким id отдаём поток
void SoundCloudService::streamAs(const QString& soundcloudId, const QString& trackId) {
    QUrl url(m_api + "/tracks/" + soundcloudId);
    QUrlQuery q;
    addClientId(q);
    url.setQuery(q);

    net->get(url, authHeaders(), [this, trackId](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit streamFailed(trackId);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray transcodings = doc.object()["media"].toObject()["transcodings"].toArray();
        QString transcodingUrl;
        int detectedBitrate = 128;

        if (m_quality == "256" || m_quality == "hq" || m_quality == "high") {
            for (const QJsonValue& t : transcodings) {
                QJsonObject to = t.toObject();
                QString qStr = to["quality"].toString();
                QString preset = to["preset"].toString();
                QString mime = to["format"].toObject()["mime_type"].toString();
                if (qStr == "hq" || preset.contains("256") || preset.contains("hq") || mime.contains("mp4a.40.2")) {
                    transcodingUrl = to["url"].toString();
                    detectedBitrate = 256;
                    break;
                }
            }
        }
        if (transcodingUrl.isEmpty()) {
            for (const QJsonValue& t : transcodings) {
                QJsonObject to = t.toObject();
                if (to["format"].toObject()["protocol"].toString() == "progressive") {
                    transcodingUrl = to["url"].toString();
                    detectedBitrate = 128;
                    break;
                }
            }
        }
        if (transcodingUrl.isEmpty() && !transcodings.isEmpty()) {
            transcodingUrl = transcodings[0].toObject()["url"].toString();
            detectedBitrate = 128;
        }

        if (!transcodingUrl.isEmpty()) {
            fetchStreamUrl(trackId, transcodingUrl, detectedBitrate);
        } else {
            emit streamFailed(trackId);
        }
    });
}

void SoundCloudService::fetchStreamUrl(const QString& trackId, const QString& transcodingUrl, int bitrate) {
    QUrl url(transcodingUrl);
    QUrlQuery q;
    addClientId(q);
    url.setQuery(q);

    net->get(url, authHeaders(), [this, trackId, bitrate](QNetworkReply* reply) {
        QString streamUrl = QJsonDocument::fromJson(reply->readAll()).object()["url"].toString();
        if (reply->error() != QNetworkReply::NoError || streamUrl.isEmpty()) {
            emit streamFailed(trackId);
            return;
        }
        emit bitrateReady(trackId, bitrate);
        emit streamUrlReady(trackId, streamUrl);
    });
}
