#include "CandidateFilter.h"
#include "TrackMatch.h"
#include "../services/YandexService.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <memory>

// короче это превью и нарезки, длиннее сеты и альбомы одним файлом
static const qint64 kMinMs = 75 * 1000;
static const qint64 kMaxMs = 12 * 60 * 1000;
// за раз в каталог уходит столько вопросов, остальные пройдут без сверки
static const int kLookupsPerBatch = 8;
// не нашлось в Яндексе: спросить снова через две недели, вдруг добавят
static const qint64 kMissRetrySecs = 14 * 24 * 3600;
static const int kCacheLimit = 4000;
// та же уверенность, что у фолбэка и связей MAIN
static const double kSameSong = 0.85;

CandidateFilter::CandidateFilter(YandexService* yandex, const QString& cachePath, QObject* parent)
    : QObject(parent), yandex(yandex), m_cachePath(cachePath) {
    load();
}

QString CandidateFilter::rejectReason(const QVariantMap& track) {
    // у кириллицы \b не работает, поэтому границы слов только у латиницы и цифр
    static const QRegularExpression junk("type beat|free beat|full album|альбом целиком|megamix|mixtape|playlist|"
                                         "плейлист|podcast|подкаст|interview|интервью|#shorts|tiktok|тикток|"
                                         "snippet|сниппет|teaser|тизер|\\b\\d+\\s?(hours?|час)",
                                         QRegularExpression::CaseInsensitiveOption);
    QString title = track["title"].toString();
    if (title.trimmed().isEmpty()) return "no title";
    qint64 ms = track["durationMs"].toLongLong();
    if (ms > 0 && ms < kMinMs) return "too short";
    if (ms > kMaxMs) return "too long";
    if (TrackMatch::isOtherVersion(track)) return "other version";
    if (junk.match(title).hasMatch()) return "not a song";
    return {};
}

void CandidateFilter::process(const QVariantList& tracks, const QString& origin,
                              std::function<void(const QVariantList&)> done) {
    QList<QVariantMap> clean;
    QSet<QString> seen;
    for (const QVariant& v : tracks) {
        QVariantMap t = TrackMatch::cleaned(v.toMap());
        QString key = TrackMatch::key(t);
        if (!rejectReason(t).isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        t["via"] = origin;
        clean.append(t);
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();
    QList<int> lookups;
    // без токена Яндекс отвечает пустым списком, и все треки записались бы в «нет в каталоге»
    if (yandex && yandex->hasToken()) {
        for (int i = 0; i < clean.size() && lookups.size() < kLookupsPerBatch; ++i) {
            auto known = m_catalog.constFind(TrackMatch::key(clean[i]));
            bool fresh =
                known != m_catalog.constEnd() && (!known->yandex.isEmpty() || now - known->checkedAt < kMissRetrySecs);
            if (!fresh) lookups.append(i);
        }
    }

    auto finish = [this, clean, origin, done]() {
        QVariantList out;
        for (const QVariantMap& t : clean) out.append(resolved(t, origin));
        done(out);
    };
    if (lookups.isEmpty()) {
        finish();
        return;
    }

    auto left = std::make_shared<int>(lookups.size());
    for (int i : std::as_const(lookups)) {
        QVariantMap t = clean[i];
        yandex->searchTracks(TrackMatch::searchQuery(t), [this, t, now, left, finish](const QVariantList& found) {
            QVariantMap best;
            double bestScore = kSameSong;
            for (const QVariant& v : found) {
                // подписи Яндекса чистые, поэтому они «искомое», а кандидат сравнивается с ними
                double score = TrackMatch::score(v.toMap(), t);
                if (score >= bestScore) {
                    bestScore = score;
                    best = v.toMap();
                }
            }
            if (!best.isEmpty()) best["available"] = yandex->isAvailable(best["id"].toString());
            m_catalog[TrackMatch::key(t)] = {best, now};
            if (--*left > 0) return;
            save();
            finish();
        });
    }
}

QVariantMap CandidateFilter::resolved(const QVariantMap& track, const QString& origin) const {
    auto known = m_catalog.constFind(TrackMatch::key(track));
    if (known == m_catalog.constEnd() || known->yandex.isEmpty()) return track;

    QVariantMap y = known->yandex;
    if (y["available"].toBool() && yandex->isAvailable(y["id"].toString())) {
        // та же песня есть в Яндексе: играет оттуда, качество лучше и не нужен VPN
        y.remove("available");
        y["via"] = origin;
        return y;
    }
    // в Яндексе убрана: играет своя версия, но с настоящими артистом и названием
    QVariantMap out = track;
    out["artist"] = y["artist"];
    out["title"] = y["title"];
    if (y.contains("artistId")) out["artistId"] = y["artistId"];
    return out;
}

void CandidateFilter::load() {
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        QJsonObject entry = it.value().toObject();
        m_catalog.insert(it.key(), {entry["y"].toObject().toVariantMap(), entry["at"].toInteger()});
    }
}

void CandidateFilter::save() const {
    QList<QPair<qint64, QString>> byAge;
    for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it) byAge.append({it->checkedAt, it.key()});
    // при переполнении уходят самые давние ответы
    std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (byAge.size() > kCacheLimit) byAge.resize(kCacheLimit);

    static const QStringList fields = {"id",       "service", "artist", "title",    "durationMs",
                                       "coverUrl", "album",   "webUrl", "artistId", "available"};
    QJsonObject saved;
    for (const auto& [at, key] : std::as_const(byAge)) {
        QVariantMap y;
        QVariantMap full = m_catalog.value(key).yandex;
        for (const QString& f : fields) {
            if (full.contains(f)) y[f] = full[f];
        }
        saved.insert(key, QJsonObject{{"y", QJsonObject::fromVariantMap(y)}, {"at", at}});
    }
    QFile file(m_cachePath);
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
}
