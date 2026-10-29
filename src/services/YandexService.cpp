#include "YandexService.h"
#include "TrackData.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QDateTime>

// адрес API можно подменить окружением, так тесты ходят в локальный фейковый сервер
YandexService::YandexService(NetworkManager* network, QObject* parent)
    : BaseService(parent), net(network),
      m_api(qEnvironmentVariable("MORPH_YANDEX_API", "https://api.music.yandex.net")) {}

static QMap<QString, QByteArray> yandexHeaders(const QString& token) {
    QMap<QString, QByteArray> h;
    h["User-Agent"] = "Yandex-Music-Desktop/5.92.1";
    h["X-Yandex-Music-Client"] = "WindowsDesktop/5.92.1";
    h["Accept"] = "application/json";
    h["X-Retpath-Y"] = "https://music.yandex.ru/";
    h["Accept-Language"] = "ru";
    if (!token.isEmpty()) {
        h["Authorization"] = "OAuth " + token.toUtf8();
    }
    return h;
}

void YandexService::setToken(const QString& token) {
    if (token == m_token) return;

    m_token = token;
    m_waveSession.clear();
    m_waveBatchOf.clear();
    m_waveQueue.clear();
}

void YandexService::setAudioQuality(const QString& quality) {
    m_quality = quality;
}

QVariantList YandexService::parseYandexTracks(const QJsonArray& tracks) {
    QVariantList tracksList;
    for (const QJsonValue& val : tracks) {
        QJsonObject wrapper = val.toObject();
        QJsonObject trackObj = wrapper.contains("track") ? wrapper["track"].toObject() : wrapper;
        if (trackObj.isEmpty()) continue;

        TrackData track;
        track.id = trackObj["id"].toVariant().toString();
        track.title = trackObj["title"].toString();

        if (trackObj.contains("durationMs")) {
            track.durationMs = trackObj["durationMs"].toVariant().toLongLong();
        } else if (wrapper.contains("durationMs")) {
            track.durationMs = wrapper["durationMs"].toVariant().toLongLong();
        } else if (trackObj.contains("duration")) {
            track.durationMs = trackObj["duration"].toVariant().toLongLong() * 1000;
        }

        QJsonArray artists = trackObj["artists"].toArray();
        QStringList artistNames;
        for (const QJsonValue& a : artists) artistNames.append(a.toObject()["name"].toString());
        track.artist = artistNames.join(", ");

        QJsonArray albums = trackObj["albums"].toArray();
        if (!albums.isEmpty()) {
            QString albumId = QString::number(albums[0].toObject()["id"].toInt());
            m_albumIds[track.id] = albumId;
            track.album = albums[0].toObject()["title"].toString();
            if (track.album.isEmpty()) track.album = albumId;
            QString coverUri = albums[0].toObject()["coverUri"].toString();
            if (!coverUri.isEmpty()) track.coverUrl = "https://" + coverUri.replace("%%", "400x400");
            track.webUrl = "https://music.yandex.ru/album/" + albumId + "/track/" + track.id;
        } else if (trackObj.contains("coverUri")) {
            track.coverUrl = "https://" + trackObj["coverUri"].toString().replace("%%", "400x400");
            track.webUrl = "https://music.yandex.ru/track/" + track.id;
        } else {
            track.webUrl = "https://music.yandex.ru/track/" + track.id;
        }
        track.service = "Yandex";
        m_durations[track.id] = track.durationMs;
        tracksList.append(track.toVariantMap());
    }
    return tracksList;
}

QString YandexService::fullTrackId(const QString& trackId) const {
    QString albumId = m_albumIds.value(trackId);
    return albumId.isEmpty() ? trackId : trackId + ":" + albumId;
}

void YandexService::search(const QString& query) {
    if (m_token.isEmpty()) return;

    QRegularExpression trackRe("track/(\\d+)");
    QRegularExpressionMatch trackMatch = trackRe.match(query);
    if (trackMatch.hasMatch()) {
        QString trackId = trackMatch.captured(1);
        QUrl url(m_api + "/tracks/" + trackId);
        net->get(url, yandexHeaders(m_token), [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) return;
            QJsonArray tracks = QJsonDocument::fromJson(reply->readAll()).object()["result"].toArray();
            emit searchResultsReady("Yandex", parseYandexTracks(tracks));
        });
        return;
    }

    QUrl url(m_api + "/search");
    QUrlQuery q;
    q.addQueryItem("text", query);
    q.addQueryItem("type", "track");
    q.addQueryItem("page", "0");
    q.addQueryItem("pageSize", "100");
    url.setQuery(q);

    net->get(url, yandexHeaders(m_token), [this](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        QByteArray data = reply->readAll();
        QJsonArray tracks = QJsonDocument::fromJson(data).object()["result"].toObject()["tracks"].toObject()["results"].toArray();
        emit searchResultsReady("Yandex", parseYandexTracks(tracks));
    });
}

void YandexService::getCharts() {
    if (m_token.isEmpty()) return;

    QUrl url(m_api + "/users/414787002/playlists/1076");
    net->get(url, yandexHeaders(m_token), [this](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonArray tracks = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject()["tracks"].toArray();
        emit chartsReady("Yandex", parseYandexTracks(tracks));
    });
}

// Волна через сессии, как в нынешних клиентах Яндекса: сессия выдаёт партии треков
// и принимает фидбек (старт, скип, дослушал), по нему волна и подстраивается.
// Старая станция осталась запасным вариантом, если сессия не создалась
void YandexService::getWave() {
    if (m_token.isEmpty()) return;

    // QML просит волну из нескольких мест подряд, вторая параллельная сессия ни к чему
    if (m_waveLoading) return;
    m_waveLoading = true;

    if (m_waveSession.isEmpty()) {
        startWaveSession();
        return;
    }

    QJsonObject body;
    body["queue"] = QJsonArray::fromStringList(m_waveQueue);

    QMap<QString, QByteArray> headers = yandexHeaders(m_token);
    headers["Content-Type"] = "application/json";
    QUrl url(QString("%1/rotor/session/%2/tracks").arg(m_api, m_waveSession));
    net->post(
        url, QJsonDocument(body).toJson(QJsonDocument::Compact), headers,
        [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                getWaveFromStation();
                return;
            }

            QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject();
            if (result["unknownSession"].toBool()) {
                m_waveSession.clear();
                startWaveSession();
                return;
            }
            handleWaveBatch(result);
        },
        false);
}

void YandexService::startWaveSession() {
    QJsonObject body;
    body["seeds"] = QJsonArray{"user:onyourwave"};
    body["includeTracksInResponse"] = true;
    if (!m_waveQueue.isEmpty()) body["queue"] = QJsonArray::fromStringList(m_waveQueue);

    QMap<QString, QByteArray> headers = yandexHeaders(m_token);
    headers["Content-Type"] = "application/json";
    QUrl url(m_api + "/rotor/session/new");
    net->post(
        url, QJsonDocument(body).toJson(QJsonDocument::Compact), headers,
        [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                getWaveFromStation();
                return;
            }

            QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject();
            m_waveSession = result["radioSessionId"].toString();
            if (m_waveSession.isEmpty()) {
                getWaveFromStation();
                return;
            }

            handleWaveBatch(result);
            sendWaveFeedback("radioStarted");
        },
        false);
}

void YandexService::handleWaveBatch(const QJsonObject& result) {
    m_waveLoading = false;
    m_waveBatch = result["batchId"].toString();

    QVariantList tracks = parseYandexTracks(result["sequence"].toArray());
    for (const QVariant& t : tracks) {
        QString id = t.toMap()["id"].toString();
        m_waveBatchOf[id] = m_waveBatch;
        m_waveQueue.append(fullTrackId(id));
    }

    // серверу нужен список уже выданного, чтобы не повторяться, хвоста хватает
    while (m_waveQueue.size() > 100) m_waveQueue.removeFirst();

    emit waveReady("Yandex", tracks);
}

void YandexService::getWaveFromStation() {
    QUrl url(m_api + "/rotor/station/user:onyourwave/tracks");
    net->get(url, yandexHeaders(m_token), [this](QNetworkReply* reply) {
        m_waveLoading = false;
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonArray sequence = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject()["sequence"].toArray();
        emit waveReady("Yandex", parseYandexTracks(sequence));
    });
}

void YandexService::sendWaveFeedback(const QString& type, const QString& trackId, double playedSeconds) {
    if (m_waveSession.isEmpty()) return;

    QJsonObject event;
    event["type"] = type;
    event["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!trackId.isEmpty()) event["trackId"] = fullTrackId(trackId);
    if (playedSeconds >= 0) event["totalPlayedSeconds"] = playedSeconds;

    QJsonObject body;
    body["event"] = event;
    QString batchId = trackId.isEmpty() ? m_waveBatch : m_waveBatchOf.value(trackId, m_waveBatch);
    if (!batchId.isEmpty()) body["batchId"] = batchId;

    QMap<QString, QByteArray> headers = yandexHeaders(m_token);
    headers["Content-Type"] = "application/json";
    QUrl url(QString("%1/rotor/session/%2/feedback").arg(m_api, m_waveSession));
    net->post(url, QJsonDocument(body).toJson(QJsonDocument::Compact), headers, [](QNetworkReply*) {}, false);
}

void YandexService::getDailyMixes() {
    emit dailyMixesReady("Yandex", QVariantList());
}

void YandexService::importPlaylist(const QString& url) {
    if (m_token.isEmpty()) return;

    QRegularExpression classicRe("users/([^/]+)/playlists/(\\d+)");
    QRegularExpression sharedRe("playlists/([a-zA-Z0-9\\.-]+)");

    QRegularExpressionMatch classicMatch = classicRe.match(url);
    QRegularExpressionMatch sharedMatch = sharedRe.match(url);

    if (classicMatch.hasMatch()) {
        QString user = classicMatch.captured(1);
        QString id = classicMatch.captured(2);
        QUrl apiUrl(QString("%1/users/%2/playlists/%3").arg(m_api, user, id));
        net->get(apiUrl, yandexHeaders(m_token), [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                emit errorOccurred("Yandex API Error: " + reply->errorString());
                return;
            }
            QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject();
            QString name = result["title"].toString();
            QString coverUrl = "";
            if (result.contains("cover")) coverUrl = "https://" + result["cover"].toObject()["uri"].toString().replace("%%", "400x400");
            else if (result.contains("ogImage")) coverUrl = "https://" + result["ogImage"].toString().replace("%%", "400x400");
            emit playlistImported(name, coverUrl, parseYandexTracks(result["tracks"].toArray()));
        });
    } else if (sharedMatch.hasMatch()) {
        QString uuid = sharedMatch.captured(1);

        QUrl apiUrl(QString("%1/playlist/%2").arg(m_api, uuid));
        QUrlQuery q;
        q.addQueryItem("rich-tracks", "true");
        apiUrl.setQuery(q);

        net->get(apiUrl, yandexHeaders(m_token), [this, apiUrl](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                QByteArray body = reply->readAll();
                emit errorOccurred(QString("Yandex API Error: %1\nURL: %2\nResponse: %3").arg(reply->errorString(), apiUrl.toString(), QString(body)));
                return;
            }
            QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject();
            if (result.isEmpty()) {
                emit errorOccurred("Playlist not found (UUID)");
                return;
            }
            QString name = result["title"].toString();
            QString coverUrl = "";
            if (result.contains("cover")) coverUrl = "https://" + result["cover"].toObject()["uri"].toString().replace("%%", "400x400");
            else if (result.contains("ogImage")) coverUrl = "https://" + result["ogImage"].toString().replace("%%", "400x400");
            emit playlistImported(name, coverUrl, parseYandexTracks(result["tracks"].toArray()));
        });
    } else {
        emit errorOccurred("Invalid Yandex Music playlist URL");
    }
}

void YandexService::resolveStreamUrl(const QString& trackId) {
    if (m_token.isEmpty()) return;

    QUrl url(QString("%1/tracks/%2/download-info").arg(m_api, trackId));
    net->get(url, yandexHeaders(m_token), [this, trackId](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray results = doc.object()["result"].toArray();
        if (results.isEmpty()) return;

        int targetBitrate = 320;
        if (m_quality == "192" || m_quality == "low") targetBitrate = 192;
        else targetBitrate = 320;

        int bestIdx = 0;
        int minDiff = 10000;

        for (int i = 0; i < results.size(); ++i) {
            int br = results[i].toObject()["bitrateInKbps"].toInt();
            int diff = qAbs(br - targetBitrate);
            if (diff < minDiff) {
                minDiff = diff;
                bestIdx = i;
            }
        }

        int br = results[bestIdx].toObject()["bitrateInKbps"].toInt();
        int targetBr = br > 0 ? br : targetBitrate;
        QString downloadInfoUrl = results[bestIdx].toObject()["downloadInfoUrl"].toString();
        fetchDownloadInfo(trackId, QUrl(downloadInfoUrl), targetBr);
    });
}

#include <QCryptographicHash>
#include <QXmlStreamReader>

void YandexService::reportPlay(const QString& trackId, const QString& albumId) {
    // Раньше тут в момент старта уходил play-audio с длиной 180 и нулём прослушанных
    // секунд, а вместо id альбома его название. Теперь статистика уходит в
    // reportListen, когда понятно, сколько трек на самом деле слушали
    Q_UNUSED(albumId);
    if (m_token.isEmpty()) return;
    if (m_waveBatchOf.contains(trackId)) sendWaveFeedback("trackStarted", trackId);
}

void YandexService::reportListen(const QString& trackId, double playedSeconds, double lengthSeconds, bool completed) {
    if (m_token.isEmpty()) return;

    if (lengthSeconds <= 0) lengthSeconds = m_durations.value(trackId) / 1000.0;
    QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString albumId = m_albumIds.value(trackId);

    QUrlQuery q;
    q.addQueryItem("track-id", trackId);
    if (!albumId.isEmpty() && albumId != "0") q.addQueryItem("album-id", albumId);
    q.addQueryItem("from", "desktop-win");
    q.addQueryItem("play-id", QString::number(QDateTime::currentMSecsSinceEpoch()) + "-morph");
    q.addQueryItem("timestamp", timestamp);
    q.addQueryItem("client-now", timestamp);
    q.addQueryItem("track-length-seconds", QString::number(lengthSeconds, 'f', 1));
    q.addQueryItem("total-played-seconds", QString::number(playedSeconds, 'f', 1));
    q.addQueryItem("end-position-seconds", QString::number(completed ? lengthSeconds : playedSeconds, 'f', 1));

    QMap<QString, QByteArray> headers = yandexHeaders(m_token);
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    net->post(
        QUrl(m_api + "/play-audio"), q.toString(QUrl::FullyEncoded).toUtf8(), headers, [](QNetworkReply*) {}, false);

    if (m_waveBatchOf.contains(trackId)) sendWaveFeedback(completed ? "trackFinished" : "skip", trackId, playedSeconds);
}

void YandexService::fetchDownloadInfo(const QString& trackId, const QUrl& url, int bitrate) {
    QMap<QString, QByteArray> h;
    h["User-Agent"] = "Yandex-Music-Desktop/5.92.1";
    h["X-Yandex-Music-Client"] = "WindowsDesktop/5.92.1";
    h["Accept"] = "*/*";
    h["X-Retpath-Y"] = "https://music.yandex.ru/";
    h["Accept-Language"] = "ru";

    net->get(url, h, [this, trackId, bitrate](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) return;

        QByteArray data = reply->readAll();
        QXmlStreamReader xml(data);

        QString host, path, ts, s;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == u"host") host = xml.readElementText();
                else if (xml.name() == u"path") path = xml.readElementText();
                else if (xml.name() == u"ts") ts = xml.readElementText();
                else if (xml.name() == u"s") s = xml.readElementText();
            }
        }

        if (host.isEmpty() || path.isEmpty()) return;

        QString sign = QCryptographicHash::hash(
            ("XGRlNCpTE_w0pS7i-709aj9m_ms9ndv" + path.mid(1) + s).toUtf8(),
            QCryptographicHash::Md5
        ).toHex();

        QString streamUrl = QString("https://%1/get-mp3/%2/%3%4")
            .arg(host).arg(sign).arg(ts).arg(path);

        emit streamUrlReady(trackId, streamUrl);
        emit bitrateReady(trackId, bitrate);
    });
}
