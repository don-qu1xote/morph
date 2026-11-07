#include "KnowledgeBase.h"
#include "../services/LastFmApi.h"
#include "TrackMatch.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

// ответ живёт месяц
static const qint64 kFreshSeconds = 30LL * 24 * 3600;
// не ответили: десять минут не спрашиваем снова
static const qint64 kRetrySeconds = 10 * 60;

static QString factKey(const QString& kind, const QString& key) {
    return kind + '\n' + key;
}

static QString artistKeyOf(const QString& name) {
    return TrackMatch::artistKey(QVariantMap{{"artist", name}});
}

KnowledgeBase::KnowledgeBase(LastFmApi* api, const QString& path, QObject* parent)
    : QObject(parent), api(api), m_connection(QString("knowledge-%1").arg(quintptr(this))),
      m_now([] { return QDateTime::currentSecsSinceEpoch(); }) {
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        // без SQLite знание живёт только до выхода, но всё остальное работает
        qWarning() << "knowledge base:" << m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("CREATE TABLE IF NOT EXISTS facts (kind TEXT NOT NULL, key TEXT NOT NULL, name TEXT, "
           "fetched INTEGER NOT NULL, data TEXT NOT NULL, PRIMARY KEY (kind, key))");
    q.exec("SELECT kind, key, name, fetched, data FROM facts");
    while (q.next()) {
        Fact fact;
        fact.name = q.value(2).toString();
        fact.fetched = q.value(3).toLongLong();
        for (const auto& v : QJsonDocument::fromJson(q.value(4).toByteArray()).array()) {
            QJsonArray row = v.toArray();
            fact.items.append({row[0].toString(), row[1].toString(), row[2].toDouble(), qint64(row[3].toDouble())});
        }
        m_facts.insert(factKey(q.value(0).toString(), q.value(1).toString()), fact);
    }
}

KnowledgeBase::~KnowledgeBase() {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

bool KnowledgeBase::hasSource() const {
    return api && api->isUsable();
}

const KnowledgeBase::Fact* KnowledgeBase::fresh(const QString& kind, const QString& key) const {
    auto it = m_facts.constFind(factKey(kind, key));
    if (it == m_facts.cend() || m_now() - it->fetched > kFreshSeconds) return nullptr;
    return &it.value();
}

void KnowledgeBase::store(const QString& kind, const QString& key, const Fact& fact) {
    m_facts.insert(factKey(kind, key), fact);
    if (m_db.isOpen()) {
        QJsonArray data;
        for (const Neighbor& n : fact.items) data.append(QJsonArray{n.artist, n.title, n.match, double(n.durationMs)});
        QSqlQuery q(m_db);
        q.prepare("INSERT OR REPLACE INTO facts (kind, key, name, fetched, data) VALUES (?, ?, ?, ?, ?)");
        q.addBindValue(kind);
        q.addBindValue(key);
        q.addBindValue(fact.name);
        q.addBindValue(fact.fetched);
        q.addBindValue(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
        if (!q.exec()) qWarning() << "knowledge base:" << q.lastError().text();
    }
    emit learned();
}

void KnowledgeBase::ask(const QString& kind, const QString& key, const QString& name, Fetch fetch, Done done) {
    if (key.isEmpty()) {
        done({});
        return;
    }
    if (const Fact* fact = fresh(kind, key)) {
        done(fact->items);
        return;
    }
    QString k = factKey(kind, key);
    // старый ответ лучше никакого, пока сети нет или нет ключа
    auto stale = [this, k]() { return m_facts.value(k).items; };
    if (!hasSource() || m_now() - m_failed.value(k) < kRetrySeconds) {
        done(stale());
        return;
    }
    auto& waiting = m_waiting[k];
    waiting.append(std::move(done));
    if (waiting.size() > 1) return;

    fetch([this, kind, key, name, k, stale](bool ok, const QList<Neighbor>& items) {
        if (ok) {
            store(kind, key, {name, m_now(), items});
            m_failed.remove(k);
        } else {
            m_failed.insert(k, m_now());
        }
        const QList<Done> waiters = m_waiting.take(k);
        const QList<Neighbor> answer = ok ? items : stale();
        for (const Done& w : waiters) w(answer);
    });
}

static QList<KnowledgeBase::Neighbor> neighbors(const QList<LastFmApi::Item>& items) {
    QList<KnowledgeBase::Neighbor> out;
    for (const LastFmApi::Item& i : items) {
        if (out.size() >= KnowledgeBase::kNeighbors) break;
        out.append({i.artist, i.title, i.match, i.durationMs});
    }
    return out;
}

void KnowledgeBase::similarTracks(const QVariantMap& track, Done done) {
    QString artist = TrackMatch::mainArtist(track);
    QString title = track["title"].toString();
    ask(
        "track", TrackMatch::songKey(track), artist + " - " + title,
        [this, artist, title](auto reply) {
            api->similarTracks(artist, title, kNeighbors,
                               [reply](bool ok, const QList<LastFmApi::Item>& items) { reply(ok, neighbors(items)); });
        },
        std::move(done));
}

void KnowledgeBase::similarArtists(const QString& artist, Done done) {
    ask(
        "artist", artistKeyOf(artist), artist,
        [this, artist](auto reply) {
            api->similarArtists(artist, kNeighbors,
                                [reply](bool ok, const QList<LastFmApi::Item>& items) { reply(ok, neighbors(items)); });
        },
        std::move(done));
}

void KnowledgeBase::topTracks(const QString& artist, Done done) {
    ask(
        "top", artistKeyOf(artist), artist,
        [this, artist](auto reply) {
            api->topTracks(artist, 20,
                           [reply](bool ok, const QList<LastFmApi::Item>& items) { reply(ok, neighbors(items)); });
        },
        std::move(done));
}

struct KnowledgeBase::Snapshot::Facts {
    QHash<QString, KnowledgeBase::Fact> all;
};

KnowledgeBase::Snapshot KnowledgeBase::snapshot() const {
    Snapshot s;
    s.m_facts = std::make_shared<const Snapshot::Facts>(Snapshot::Facts{m_facts});
    return s;
}

QList<KnowledgeBase::Neighbor> KnowledgeBase::Snapshot::similarTracks(const QVariantMap& track) const {
    if (!m_facts) return {};
    return m_facts->all.value(factKey("track", TrackMatch::songKey(track))).items;
}

QList<KnowledgeBase::Neighbor> KnowledgeBase::knownSimilarTracks(const QVariantMap& track) const {
    return m_facts.value(factKey("track", TrackMatch::songKey(track))).items;
}

QList<KnowledgeBase::Neighbor> KnowledgeBase::knownSimilarArtists(const QString& artist) const {
    return m_facts.value(factKey("artist", artistKeyOf(artist))).items;
}

QList<KnowledgeBase::Neighbor> KnowledgeBase::knownTopTracks(const QString& artist) const {
    return m_facts.value(factKey("top", artistKeyOf(artist))).items;
}

QList<QPair<QString, QList<KnowledgeBase::Neighbor>>> KnowledgeBase::allSimilarArtists() const {
    QList<QPair<QString, QList<Neighbor>>> out;
    for (auto it = m_facts.cbegin(); it != m_facts.cend(); ++it) {
        if (it.key().startsWith("artist\n")) out.append({it->name, it->items});
    }
    return out;
}

void KnowledgeBase::warm(const QStringList& artists) {
    for (const QString& a : artists) {
        if (!a.isEmpty() && !m_warm.contains(a) && !fresh("artist", artistKeyOf(a))) m_warm.append(a);
    }
    warmNext();
}

void KnowledgeBase::warmNext() {
    if (m_warming) return;
    while (!m_warm.isEmpty() && fresh("artist", artistKeyOf(m_warm.first()))) m_warm.removeFirst();
    if (m_warm.isEmpty() || !hasSource()) {
        m_warm.clear();
        return;
    }
    m_warming = true;
    similarArtists(m_warm.takeFirst(), [this](const QList<Neighbor>&) {
        m_warming = false;
        // через цикл событий: из базы ответ приходит сразу, без этого была бы рекурсия
        QTimer::singleShot(0, this, &KnowledgeBase::warmNext);
    });
}
