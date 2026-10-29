#include "LyricsService.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>

// ключ подписи из Android-приложения Яндекс Музыки, тот же, что у yandex-music-api
static const QByteArray kYandexSignKey = "p93jhgh689SBReK6ghtw62";

LyricsService::LyricsService(NetworkManager* network, QObject* parent)
    : QObject(parent), net(network),
      m_yandexApi(qEnvironmentVariable("MORPH_YANDEX_API", "https://api.music.yandex.net")),
      m_lrclibApi(qEnvironmentVariable("MORPH_LRCLIB_API", "https://lrclib.net/api")) {}

void LyricsService::fetch(const QVariantMap& track) {
    QString trackId = track["id"].toString();
    auto found = m_found.constFind(trackId);
    if (found != m_found.constEnd()) {
        emit lyricsReady(trackId, found->value("lines").toList(), found->value("synced").toBool(),
                         found->value("source").toString());
        return;
    }

    if (track["service"].toString() == "Yandex" && !m_yandexToken.isEmpty()) fetchFromYandex(track);
    else fetchFromLrclib(track);
}

// /tracks/{id}/lyrics подписывается: HMAC-SHA256 от id трека и времени
void LyricsService::fetchFromYandex(const QVariantMap& track) {
    QString trackId = track["id"].toString();
    QString numericId = trackId.section(':', 0, 0);
    QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());
    QByteArray sign =
        QMessageAuthenticationCode::hash((numericId + timestamp).toUtf8(), kYandexSignKey, QCryptographicHash::Sha256)
            .toBase64();

    // подпись в base64, и «+» в ней обязан уйти как %2B, иначе сервер прочтёт пробел
    QUrl url(m_yandexApi + "/tracks/" + numericId + "/lyrics");
    url.setQuery(
        QString("format=LRC&timeStamp=%1&sign=%2").arg(timestamp, QString::fromLatin1(QUrl::toPercentEncoding(sign))),
        QUrl::StrictMode);

    QMap<QString, QByteArray> headers;
    headers["Authorization"] = "OAuth " + m_yandexToken.toUtf8();
    net->get(
        url, headers,
        [this, track, trackId](QNetworkReply* reply) {
            QString downloadUrl =
                QJsonDocument::fromJson(reply->readAll()).object()["result"].toObject()["downloadUrl"].toString();
            if (reply->error() != QNetworkReply::NoError || downloadUrl.isEmpty()) {
                fetchFromLrclib(track);
                return;
            }
            net->get(
                QUrl(downloadUrl), {},
                [this, track, trackId](QNetworkReply* reply) {
                    QString text = QString::fromUtf8(reply->readAll());
                    if (reply->error() != QNetworkReply::NoError || text.trimmed().isEmpty()) fetchFromLrclib(track);
                    else deliver(trackId, text, "Yandex");
                },
                false);
        },
        false);
}

// Сначала точный запрос по артисту, названию и длительности, если не нашлось,
// обычный поиск по тем же словам
void LyricsService::fetchFromLrclib(const QVariantMap& track, bool searchInstead) {
    QString trackId = track["id"].toString();
    QString artist = track["artist"].toString().section(',', 0, 0).trimmed();
    QString title = track["title"].toString();

    QUrl url(m_lrclibApi + (searchInstead ? "/search" : "/get"));
    QUrlQuery q;
    if (searchInstead) {
        q.addQueryItem("q", artist + " " + title);
    } else {
        q.addQueryItem("artist_name", artist);
        q.addQueryItem("track_name", title);
        qint64 seconds = track["durationMs"].toLongLong() / 1000;
        if (seconds > 0) q.addQueryItem("duration", QString::number(seconds));
    }
    url.setQuery(q);

    QMap<QString, QByteArray> headers;
    headers["User-Agent"] = "morph (https://github.com/don-qu1xote/morph)";
    net->get(
        url, headers,
        [this, track, trackId, searchInstead](QNetworkReply* reply) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject found = searchInstead ? doc.array().first().toObject() : doc.object();
            QString text = found["syncedLyrics"].toString();
            if (text.isEmpty()) text = found["plainLyrics"].toString();

            if (reply->error() == QNetworkReply::NoError && !text.trimmed().isEmpty()) {
                deliver(trackId, text, "LRCLIB");
            } else if (!searchInstead) {
                fetchFromLrclib(track, true);
            } else {
                emit lyricsMissing(trackId);
            }
        },
        false);
}

void LyricsService::deliver(const QString& trackId, const QString& text, const QString& source) {
    QVariantList lines = parseLrc(text);
    bool synced = !lines.isEmpty() && lines.first().toMap()["timeMs"].toLongLong() >= 0;
    m_found.insert(trackId, {{"lines", lines}, {"synced", synced}, {"source", source}});
    emit lyricsReady(trackId, lines, synced, source);
}

// [01:23.45] строка; у одной строки бывает несколько меток (припев), а теги
// вроде [ar:...] и [length:...] к тексту не относятся
QVariantList LyricsService::parseLrc(const QString& text) {
    static const QRegularExpression stamp("\\[(\\d+):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]");
    static const QRegularExpression tag("^\\[[a-zA-Z]+:.*\\]$");

    QList<QPair<qint64, QString>> timed;
    QVariantList plain;
    const QStringList rows = text.split('\n');
    for (const QString& raw : rows) {
        QString row = raw.trimmed();
        if (row.isEmpty() || tag.match(row).hasMatch()) continue;

        QList<qint64> times;
        QRegularExpressionMatch m = stamp.match(row);
        while (m.hasMatch() && m.capturedStart() == 0) {
            QString fraction = m.captured(3).leftJustified(3, '0');
            times.append(m.captured(1).toLongLong() * 60000 + m.captured(2).toLongLong() * 1000 +
                         fraction.toLongLong());
            row = row.mid(m.capturedLength()).trimmed();
            m = stamp.match(row);
        }

        if (times.isEmpty()) {
            plain.append(QVariantMap{{"timeMs", -1}, {"text", row}});
            continue;
        }
        for (qint64 t : times) timed.append({t, row});
    }

    if (timed.isEmpty()) return plain;
    std::stable_sort(timed.begin(), timed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QVariantList lines;
    for (const auto& [time, line] : timed) lines.append(QVariantMap{{"timeMs", time}, {"text", line}});
    return lines;
}
