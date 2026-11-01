#ifndef MORPH_MIXES_H
#define MORPH_MIXES_H

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>

class Library;
class Recommender;
class SoundCloudService;
class YandexService;

// Плейлисты, которые собирает сам morph, а не сервис:
//   On Repeat: что заслушано за месяц;
//   Rediscover: лайки, которые давно не звучали;
//   Daily Mix: группы артистов, которых слушаешь вместе, плюс похожее новое;
//   Discover Weekly: незнакомые артисты рядом с любимым, раз в неделю;
//   Release Radar: свежие релизы любимых артистов.
// Карточки считаются на месте из истории и MAIN. Треки по клику: где нужно
// похожее или новые альбомы, это несколько запросов в Яндекс и SoundCloud
class MorphMixes : public QObject {
    Q_OBJECT
public:
    MorphMixes(Recommender* recommender, Library* library, YandexService* yandex, SoundCloudService* soundcloud,
               const QString& dataDir, QObject* parent = nullptr);

    // для «For You»: id, name, subtitle, coverUrl, service "morph", permalink_url
    QVariantList cards();
    // id вида morph:onrepeat; ответ в ready или failed
    void build(const QString& id);

    // Волна по списку треков, например по плейлисту: похожее на три случайных
    // трека из него и пара его собственных. Партии в seededBatch, следующую
    // просит nextSeeded, как getWave у Яндекса
    void startSeeded(const QVariantList& tracks);
    void nextSeeded();

    // Группы артистов для Daily Mix. Артисты рядом, если звучали в одной
    // сессии (без перерыва в полчаса) или лайкнуты друг за другом. Дальше
    // распространение меток: каждый берёт метку, у которой больше всего общего
    // с ним веса, пока метки не перестанут меняться
    QList<QStringList> artistGroups() const;

signals:
    void ready(const QString& name, const QString& coverUrl, const QVariantList& tracks);
    void failed(const QString& message);
    void seededBatch(const QVariantList& tracks);

private:
    using Tracks = std::function<void(const QVariantList&)>;

    QVariantList familiar() const;
    QVariantList onRepeat() const;
    QVariantList rediscover() const;
    QVariantList seeds(int count) const;
    QString yandexIdOf(const QVariantMap& track) const;
    QHash<QString, QString> artistIds() const;

    void buildDailyMix(int index);
    void buildDiscover();
    void buildReleaseRadar();
    // похожее на seeds из Яндекса и SoundCloud, все ответы разом
    void similarTo(const QVariantList& seeds, int soundcloudSeeds, Tracks done);

    static QVariantMap card(const QString& id, const QString& name, const QString& subtitle,
                            const QVariantList& tracks);
    int week() const;

    Recommender* rec;
    Library* library;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    QString m_discoverPath;

    QVariantList m_seeds;
    int m_seedPos = 0;
    bool m_seedBusy = false;
    // ответ по старому списку после нового старта отбрасывается
    int m_seedGeneration = 0;
};

#endif
