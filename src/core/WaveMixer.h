#ifndef WAVE_MIXER_H
#define WAVE_MIXER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <optional>

class SoundCloudService;
class YouTubeService;

// Волну по-прежнему ведёт Яндекс: его треки идут как есть и в том же порядке,
// фидбек по ним уходит в rotor-сессию. Миксер вставляет между ними похожее с
// SoundCloud и YouTube, а иногда лайк, который Яндекс убрал и сам уже не предложит.
// Похожее ищется в фоне, пока играет партия, поэтому первая партия приходит без
// задержки, а примесь появляется со следующей
class WaveMixer : public QObject {
    Q_OBJECT
public:
    WaveMixer(SoundCloudService* soundcloud, YouTubeService* youtube, QObject* parent = nullptr);

    void setEnabled(bool enabled);
    // лайки, которые Яндекс больше не отдаёт: играют через фолбэк и служат сидами
    void setLostTracks(const QVariantList& tracks);
    // дослушанный или лайкнутый трек: по нему похожее ищется в первую очередь
    void addSeed(const QVariantMap& track);
    void trackFinished(const QString& trackId);

    static QString trackKey(const QVariantMap& track);

public slots:
    void addBatch(const QString& serviceName, const QVariantList& tracks);

signals:
    void waveReady(const QString& serviceName, const QVariantList& tracks);

private:
    enum class Source { SoundCloud, YouTube, Lost };
    static Source sourceForTurn(int turn);
    static QString seedQuery(const QVariantMap& track);

    std::optional<QVariantMap> takeExtra();
    std::optional<QVariantMap> takeFrom(QList<QVariantMap>& pool);
    void refill();
    QString nextSeed();
    void addToPool(QList<QVariantMap>& pool, const QVariantList& tracks);
    bool isFresh(const QVariantMap& track) const;
    void remember(const QVariantMap& track);

    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    bool m_enabled = true;
    bool m_soundcloudBusy = false;
    bool m_youtubeBusy = false;
    int m_turn = 0;

    QList<QVariantMap> m_soundcloud;
    QList<QVariantMap> m_youtube;
    QList<QVariantMap> m_lost;

    // запросы "артист - название", свежие первыми
    QStringList m_seeds;
    QStringList m_lostSeeds;
    bool m_lostSeedTurn = false;
    QSet<QString> m_usedSeeds;
    QVariantList m_lastBatch;

    // всё, что уже ушло в волну: по id и по ключу из trackKey
    QHash<QString, QVariantMap> m_given;
    QSet<QString> m_givenKeys;
};

#endif
