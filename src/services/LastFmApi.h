#ifndef LASTFM_API_H
#define LASTFM_API_H

#include <QList>
#include <QObject>
#include <QString>
#include <QUrlQuery>
#include <functional>

class NetworkManager;
class QJsonObject;

// Last.fm: похожие треки, похожие артисты и популярное у артиста. Это знание
// о том, что слушают вместе миллионы людей на всех площадках, на нём держится
// своя модель вкуса. Ключ API у каждого свой, бесплатный.
// Запросы идут очередью, не чаще четырёх в секунду: на большее Last.fm
// отвечает ошибкой 29 и какое-то время не пускает
class LastFmApi : public QObject {
    Q_OBJECT
public:
    struct Item {
        QString artist;
        // пусто в списке артистов
        QString title;
        // похожесть от 0 до 1; у популярного доля от самого популярного
        double match = 0;
        QString mbid;
        qint64 durationMs = 0;
    };
    // ok: ответ пришёл (пустой список тоже ответ); false: сеть, ключ или лимит
    using Done = std::function<void(bool ok, const QList<Item>& items)>;

    explicit LastFmApi(NetworkManager* net, QObject* parent = nullptr);

    void setKey(const QString& key);
    bool hasKey() const { return !m_key.isEmpty(); }
    // ключ есть и Last.fm его не отклонил
    bool isUsable() const { return !m_key.isEmpty() && !m_rejected; }
    bool isRejected() const { return m_rejected; }
    // Ключ Last.fm это 32 шестнадцатеричных символа. При копировании легко
    // потерять крайний, а Last.fm на это ответит только «Invalid API key»
    static bool looksLikeKey(const QString& key);
    // сколько запросов подряд не дошло или вернулось с ошибкой, и текст последней
    int failuresInRow() const { return m_failures; }
    QString lastError() const { return m_lastError; }

    void similarTracks(const QString& artist, const QString& title, int limit, Done done);
    void similarArtists(const QString& artist, int limit, Done done);
    void topTracks(const QString& artist, int limit, Done done);
    // сколько запросов ждёт очереди
    int queued() const { return int(m_queue.size()); }

signals:
    // ключ не подошёл (ошибка 10 или 26): дальше без Last.fm, пока его не сменят
    void keyRejected();

private:
    struct Job {
        QUrlQuery query;
        std::function<QList<Item>(const QJsonObject&)> parse;
        Done done;
    };
    void enqueue(const QString& method, QUrlQuery query, std::function<QList<Item>(const QJsonObject&)> parse,
                 Done done);
    void pump();

    NetworkManager* net;
    QString m_api;
    QString m_key;
    QList<Job> m_queue;
    bool m_busy = false;
    bool m_rejected = false;
    qint64 m_pausedUntil = 0;
    int m_failures = 0;
    QString m_lastError;
};

#endif
