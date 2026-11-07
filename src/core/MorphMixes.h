#ifndef MORPH_MIXES_H
#define MORPH_MIXES_H

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <memory>

class CandidateFilter;
class Library;
class MusicSpace;
class Recommender;
class SoundCloudService;
class TasteRadio;
class YandexService;

// Плейлисты, которые собирает сам morph, а не сервис:
//   On Repeat: что заслушано за месяц;
//   Rediscover: лайки, которые давно не звучали;
//   Daily Mix: группы артистов, которых слушаешь вместе или которые рядом на
//     карте музыки, плюс похожее новое вокруг центра группы;
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
    void setFilter(CandidateFilter* filter) { this->filter = filter; }
    // Своя модель вкуса: карта сводит в группы похожих артистов и держит микс
    // вокруг центра группы, радио приносит их соседей по Last.fm
    void setTaste(std::function<std::shared_ptr<const MusicSpace>()> space, TasteRadio* radio) {
        m_space = std::move(space);
        m_radio = radio;
    }

    // Волна по списку треков, например по плейлисту: похожее на три случайных
    // трека из него и пара его собственных. Партии в seededBatch, следующую
    // просит nextSeeded, как getWave у Яндекса
    void startSeeded(const QVariantList& tracks);
    void nextSeeded();

    // Группы артистов для Daily Mix. Артисты рядом, если звучали в одной
    // сессии (без перерыва в полчаса), лайкнуты друг за другом или стоят рядом
    // на карте музыки. Дальше распространение меток: каждый берёт метку, у
    // которой больше всего общего с ним веса, пока метки не перестанут меняться
    QList<QStringList> artistGroups() const;

    // Что добавить в плейлист. С треками: похожее на несколько из них. Пустой с
    // названием: треки из плейлистов Яндекса с таким названием и песни с ним.
    // Пустой без названия (My Playlist #3): похожее на то, что ты любишь.
    // Того, что в плейлисте уже есть, в ответе нет, порядок решает своя модель.
    // basis: contents, title или taste, по чему подобрано
    using Suggestions = std::function<void(const QVariantList& tracks, const QString& basis)>;
    void suggest(const QString& name, const QVariantList& tracks, int count, Suggestions done);

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
    // соседи по Last.fm нескольких треков, уже через отбор кандидатов
    void tasteNeighbors(const QVariantList& tracks, int count, Tracks done);
    // кандидаты по названию плейлиста
    void byTitle(const QString& name, Tracks done);

    static QVariantMap card(const QString& id, const QString& name, const QString& subtitle,
                            const QVariantList& tracks);
    int week() const;

    Recommender* rec;
    Library* library;
    YandexService* yandex;
    SoundCloudService* soundcloud;
    CandidateFilter* filter = nullptr;
    std::function<std::shared_ptr<const MusicSpace>()> m_space;
    TasteRadio* m_radio = nullptr;
    QString m_discoverPath;

    QVariantList m_seeds;
    int m_seedPos = 0;
    bool m_seedBusy = false;
    // ответ по старому списку после нового старта отбрасывается
    int m_seedGeneration = 0;
};

#endif
