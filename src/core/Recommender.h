#ifndef RECOMMENDER_H
#define RECOMMENDER_H

#include "ListeningHistory.h"
#include "TasteProfile.h"
#include <QObject>
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

    // MAIN, по треку на песню: лайки за всё время для профиля
    void setLikes(const QVariantList& likes);
    void listened(const QVariantMap& track, double played, double length, bool completed);
    void liked(const QVariantMap& track, bool liked);
    void disliked(const QVariantMap& track);

    const TasteProfile& profile() const { return m_profile; }
    const ListeningHistory& history() const { return m_history; }
    // часы подменяются в тестах
    void setClock(std::function<qint64()> now) { m_now = std::move(now); }
    qint64 now() const { return m_now(); }

private:
    void record(ListenEvent event);

    std::function<qint64()> m_now;
    ListeningHistory m_history;
    TasteProfile m_profile;
};

#endif
