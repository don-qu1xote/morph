#include "LastFmApi.h"
#include "../network/NetworkManager.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

// между запросами четверть секунды: четыре в секунду Last.fm терпит
static const int kGapMs = 250;
// после ошибки 29 минута тишины
static const int kRateLimitPauseMs = 60000;

LastFmApi::LastFmApi(NetworkManager* net, QObject* parent)
    : QObject(parent), net(net), m_api(qEnvironmentVariable("MORPH_LASTFM_API", "https://ws.audioscrobbler.com/2.0/")) {
}

void LastFmApi::setKey(const QString& key) {
    QString trimmed = key.trimmed();
    if (trimmed == m_key) return;
    m_key = trimmed;
    m_rejected = false;
    m_failures = 0;
    m_lastError.clear();
}

bool LastFmApi::looksLikeKey(const QString& key) {
    static const QRegularExpression hex("^[0-9a-fA-F]{32}$");
    return hex.match(key.trimmed()).hasMatch();
}

// одиночный элемент Last.fm отдаёт объектом, а не массивом из одного
static QJsonArray list(const QJsonValue& value) {
    if (value.isArray()) return value.toArray();
    if (value.isObject()) return QJsonArray{value};
    return {};
}

// числа приходят то числами, то строками
static double number(const QJsonValue& value) {
    return value.isString() ? value.toString().toDouble() : value.toDouble();
}

void LastFmApi::similarTracks(const QString& artist, const QString& title, int limit, Done done) {
    QUrlQuery q;
    q.addQueryItem("artist", artist);
    q.addQueryItem("track", title);
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("autocorrect", "1");
    enqueue(
        "track.getsimilar", q,
        [](const QJsonObject& root) {
            QList<Item> items;
            for (const auto& v : list(root["similartracks"].toObject()["track"])) {
                QJsonObject t = v.toObject();
                Item item;
                item.artist = t["artist"].toObject()["name"].toString();
                item.title = t["name"].toString();
                item.match = number(t["match"]);
                item.mbid = t["mbid"].toString();
                item.durationMs = qint64(number(t["duration"]) * 1000);
                if (!item.artist.isEmpty() && !item.title.isEmpty()) items.append(item);
            }
            return items;
        },
        std::move(done));
}

void LastFmApi::similarArtists(const QString& artist, int limit, Done done) {
    QUrlQuery q;
    q.addQueryItem("artist", artist);
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("autocorrect", "1");
    enqueue(
        "artist.getsimilar", q,
        [](const QJsonObject& root) {
            QList<Item> items;
            for (const auto& v : list(root["similarartists"].toObject()["artist"])) {
                QJsonObject a = v.toObject();
                Item item;
                item.artist = a["name"].toString();
                item.match = number(a["match"]);
                item.mbid = a["mbid"].toString();
                if (!item.artist.isEmpty()) items.append(item);
            }
            return items;
        },
        std::move(done));
}

void LastFmApi::topTracks(const QString& artist, int limit, Done done) {
    QUrlQuery q;
    q.addQueryItem("artist", artist);
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("autocorrect", "1");
    enqueue(
        "artist.gettoptracks", q,
        [](const QJsonObject& root) {
            QList<Item> items;
            double top = 0;
            for (const auto& v : list(root["toptracks"].toObject()["track"])) {
                QJsonObject t = v.toObject();
                Item item;
                item.artist = t["artist"].toObject()["name"].toString();
                item.title = t["name"].toString();
                item.match = number(t["playcount"]);
                item.mbid = t["mbid"].toString();
                item.durationMs = qint64(number(t["duration"]) * 1000);
                top = qMax(top, item.match);
                if (!item.artist.isEmpty() && !item.title.isEmpty()) items.append(item);
            }
            for (Item& item : items) item.match = top > 0 ? item.match / top : 0;
            return items;
        },
        std::move(done));
}

void LastFmApi::enqueue(const QString& method, QUrlQuery query, std::function<QList<Item>(const QJsonObject&)> parse,
                        Done done) {
    if (m_key.isEmpty() || m_rejected) {
        done(false, {});
        return;
    }
    query.addQueryItem("method", method);
    query.addQueryItem("format", "json");
    m_queue.append({query, std::move(parse), std::move(done)});
    pump();
}

void LastFmApi::pump() {
    if (m_busy || m_queue.isEmpty()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_pausedUntil) {
        m_busy = true;
        QTimer::singleShot(int(m_pausedUntil - now), this, [this] {
            m_busy = false;
            pump();
        });
        return;
    }
    if (m_rejected) {
        // ключ отклонили, пока запросы стояли в очереди: всем отказ сразу
        const QList<Job> jobs = std::exchange(m_queue, {});
        for (const Job& job : jobs) job.done(false, {});
        return;
    }

    Job job = m_queue.takeFirst();
    QUrlQuery query = job.query;
    query.addQueryItem("api_key", m_key);
    QUrl url(m_api);
    url.setQuery(query);
    m_busy = true;
    net->get(
        url, {},
        [this, job](QNetworkReply* reply) {
            QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            int error = root["error"].toInt();
            bool ok = error == 0 && reply->error() == QNetworkReply::NoError;
            // 6: нет такого трека или артиста, это тоже ответ, просто пустой
            if (error == 6) ok = true;
            if (error == 10 || error == 26) {
                m_rejected = true;
                emit keyRejected();
            }
            if (error == 29) m_pausedUntil = QDateTime::currentMSecsSinceEpoch() + kRateLimitPauseMs;
            if (ok) {
                m_failures = 0;
                m_lastError.clear();
            } else if (!m_rejected) {
                ++m_failures;
                m_lastError = error != 0 ? root["message"].toString() : reply->errorString();
            }
            job.done(ok, ok && error == 0 ? job.parse(root) : QList<Item>());
            QTimer::singleShot(kGapMs, this, [this] {
                m_busy = false;
                pump();
            });
        },
        false);
}
