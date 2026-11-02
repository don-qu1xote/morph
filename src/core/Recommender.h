#ifndef RECOMMENDER_H
#define RECOMMENDER_H

#include "ListeningHistory.h"
#include "Ranker.h"
#include "TasteProfile.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <functional>

// Своя рекомендательная часть поверх сервисов. Сервисы дают кандидатов, а
// что из них и в каком порядке играть, решается здесь по тому, как ты
// слушаешь на всех площадках сразу. Тут история и профиль вкуса, на них
// опираются ранжирование волн и плейлисты от morph
class Recommender : public QObject {
    Q_OBJECT
public:
    explicit Recommender(const QString& dataDir, QObject* parent = nullptr);
    ~Recommender() override;

    // MAIN, по треку на песню: лайки за всё время для профиля
    void setLikes(const QVariantList& likes);
    void listened(const QVariantMap& track, double played, double length, bool completed);
    void liked(const QVariantMap& track, bool liked);
    void disliked(const QVariantMap& track);

    // Лучшие count из candidates по модели «дослушаешь ли». Доля треков
    // незнакомых артистов держится около newShare, советов SoundCloud и YouTube
    // не меньше externalShare, один артист не встаёт два раза подряд и не
    // чаще двух раз на десять треков. explore: с разведкой, newShare: своя
    // доля нового вместо общей настройки
    QVariantList rank(const QVariantList& candidates, int count, bool explore = true, double newShare = -1);
    // 0.15 больше привычного, 0.3 баланс, 0.5 больше нового
    void setNewShare(double share) { m_newShare = share; }
    double newShare() const { return m_newShare; }
    // незнакомый артист: ничего у него не дослушал и не лайкал
    bool isNew(const QVariantMap& track) const;
    // Сколько мест в волне держать за советами SoundCloud и YouTube. Их
    // кандидаты сырее яндексовских, и без нижней границы модель вытесняла бы
    // их целиком, так и не узнав, нравятся ли они тебе. 0, если смешанная выключена
    void setExternalShare(double share) { m_externalShare = share; }
    // посоветован SoundCloud или YouTube, где бы ни играл
    static bool isExternal(const QVariantMap& track);
    Ranker::Features features(const QVariantMap& track) const;

    // Партия волны ложится в запас, наружу уходят лучшие. Четверть остаётся и
    // соревнуется со следующей партией, поэтому то, что модель считает
    // скипом, так и не заиграет. Новая волна начинается с пустого запаса
    QVariantList nextWave(const QVariantList& batch);
    void resetWave();

    const TasteProfile& profile() const { return m_profile; }
    const Ranker& ranker() const { return m_ranker; }
    void setSeed(quint32 seed) { m_ranker.setSeed(seed); }
    const ListeningHistory& history() const { return m_history; }
    // часы подменяются в тестах
    void setClock(std::function<qint64()> now) { m_now = std::move(now); }
    qint64 now() const { return m_now(); }

private:
    void record(ListenEvent event);
    void learn(const ListenEvent& event, const Ranker::Features& x);
    void saveRanker();

    std::function<qint64()> m_now;
    ListeningHistory m_history;
    TasteProfile m_profile;
    Ranker m_ranker;
    QString m_rankerPath;
    int m_unsaved = 0;
    double m_newShare = 0.3;
    double m_externalShare = 0;
    // по TrackMatch::songKey, копится вместе с историей
    QHash<QString, ListeningHistory::SongStats> m_songs;
    QSet<QString> m_liked;

    struct Pending {
        QVariantMap track;
        int turn = 0;
    };
    QList<Pending> m_wavePool;
    QSet<QString> m_waveGiven;
    int m_waveTurn = 0;
};

#endif
