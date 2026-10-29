#ifndef PLAYBACK_QUEUE_H
#define PLAYBACK_QUEUE_H

#include <QList>
#include <QObject>
#include <QRandomGenerator>
#include <QSet>
#include <QVariantList>

// Очередь «играть следующим» и перемешивание. Сам список, который играет
// (плейлист, волна, поиск), живёт в QML; очередь проигрывается поверх него, а
// после неё список продолжается с того же места. В QML это MorphQueue
class PlaybackQueue : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(int count READ count NOTIFY tracksChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)

public:
    explicit PlaybackQueue(QObject* parent = nullptr);

    QVariantList tracks() const { return m_tracks; }
    int count() const { return m_tracks.size(); }
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    void setSeed(quint32 seed) { m_random.seed(seed); }

    Q_INVOKABLE void playNext(const QVariantMap& track);
    Q_INVOKABLE void add(const QVariantMap& track);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    // пустой словарь, если очередь пуста
    Q_INVOKABLE QVariantMap takeNext();
    // включили трек из середины очереди: всё до него уходит вместе с ним
    Q_INVOKABLE QVariantMap takeUpTo(int index);

    // Перемешивание внутри списка из count треков: каждый играет по разу, потом
    // новый круг. -1, если выбирать не из чего
    Q_INVOKABLE int nextIndex(int current, int count);
    // «назад» при перемешивании идёт по тому, что реально играло
    Q_INVOKABLE int previousIndex(int current);

signals:
    void tracksChanged();
    void shuffleChanged();

private:
    void restartRound(int current);

    QVariantList m_tracks;
    bool m_shuffle = false;
    int m_listSize = -1;
    QSet<int> m_left;
    QList<int> m_history;
    QRandomGenerator m_random;
};

#endif
