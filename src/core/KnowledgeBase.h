#ifndef KNOWLEDGE_BASE_H
#define KNOWLEDGE_BASE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantMap>
#include <functional>

class LastFmApi;

// Что известно о музыке вокруг твоей: похожие треки, похожие артисты и
// популярное у артиста по Last.fm. Ответ живёт месяц: что слушают вместе,
// меняется медленно, а спрашивать тысячи артистов на каждом запуске незачем.
// Хранится в SQLite (knowledge.db), строка на ответ, поэтому новое дописывается,
// а не переписывает файл целиком. Читается всё из памяти: база загружается
// при старте и дальше только принимает новое
class KnowledgeBase : public QObject {
    Q_OBJECT
public:
    struct Neighbor {
        QString artist;
        QString title;
        double match = 0;
        qint64 durationMs = 0;
    };
    using Done = std::function<void(const QList<Neighbor>&)>;

    KnowledgeBase(LastFmApi* api, const QString& path, QObject* parent = nullptr);
    ~KnowledgeBase() override;

    // из базы, если ответу меньше месяца, иначе из Last.fm с записью в базу.
    // Без ключа и при ошибке пустой список
    void similarTracks(const QVariantMap& track, Done done);
    void similarArtists(const QString& artist, Done done);
    void topTracks(const QString& artist, Done done);

    // без сети, только то, что уже есть
    QList<Neighbor> knownSimilarTracks(const QVariantMap& track) const;
    QList<Neighbor> knownSimilarArtists(const QString& artist) const;
    QList<Neighbor> knownTopTracks(const QString& artist) const;
    // все ответы про похожих артистов: имя артиста и его соседи
    QList<QPair<QString, QList<Neighbor>>> allSimilarArtists() const;
    int size() const { return int(m_facts.size()); }

    // По очереди узнать похожих для этих артистов: из них складывается
    // пространство вкуса. Уже известные пропускаются
    void warm(const QStringList& artists);
    bool hasSource() const;
    void setClock(std::function<qint64()> now) { m_now = std::move(now); }

    static const int kNeighbors = 50;

signals:
    // в базу легло новое
    void learned();

private:
    struct Fact {
        QString name;
        qint64 fetched = 0;
        QList<Neighbor> items;
    };
    using Fetch = std::function<void(std::function<void(bool, const QList<Neighbor>&)>)>;
    void ask(const QString& kind, const QString& key, const QString& name, Fetch fetch, Done done);
    const Fact* fresh(const QString& kind, const QString& key) const;
    void store(const QString& kind, const QString& key, const Fact& fact);
    void warmNext();

    LastFmApi* api;
    QSqlDatabase m_db;
    QString m_connection;
    std::function<qint64()> m_now;
    // kind + '\n' + key
    QHash<QString, Fact> m_facts;
    QHash<QString, QList<Done>> m_waiting;
    // не ответили: столько не спрашиваем снова, чтобы не долбить сеть
    QHash<QString, qint64> m_failed;
    QStringList m_warm;
    bool m_warming = false;
};

#endif
