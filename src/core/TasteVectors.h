#ifndef TASTE_VECTORS_H
#define TASTE_VECTORS_H

#include "ListeningHistory.h"
#include "MusicSpace.h"
#include <functional>
#include <optional>

// Ты как точка в пространстве музыки, в двух временах.
//
// Долгий вкус: u_L = norm(сумма w_i * v_i + сумма лайков из MAIN), где
// w_i = r_i * 0.5^(дней назад / 60), r: дослушал +1, лайк +3, скип -1,
// снял лайк -1, дизлайк -3. Сумма хранится на момент последнего события и
// при следующем просто затухает, пересчитывать всю историю не нужно.
//
// Вкус сессии: u_S = norm(0.7 * u_S + 0.3 * r * v), сессия кончается после
// получаса тишины. Два скипа подряд уводят её в сторону уже на следующем треке.
//
// Сейчас: u = norm(alpha * u_S + (1 - alpha) * u_L), alpha = min(0.6, 0.1 * треков в сессии)
class TasteVectors {
public:
    using Vector = MusicSpace::Vector;
    using VectorOf = std::function<std::optional<Vector>(const QVariantMap&)>;

    // заново по истории и лайкам: когда пространство пересобрано и векторы песен другие
    void rebuild(const QList<ListenEvent>& events, const QVariantList& likes, const VectorOf& vectorOf);
    // время событий в секундах, как в ListenEvent
    void add(const ListenEvent& event, const std::optional<Vector>& v);

    std::optional<Vector> longTerm(qint64 now) const;
    std::optional<Vector> session(qint64 now) const;
    std::optional<Vector> current(qint64 now) const;
    int sessionLength(qint64 now) const;

    // сколько весит событие в долгом вкусе и в сессии
    static double longWeight(const ListenEvent& event);
    static double sessionWeight(const ListenEvent& event);

    static constexpr qint64 kSessionGap = 30 * 60;
    static constexpr double kHalfLifeDays = 60;

private:
    void addLong(const ListenEvent& event, const Vector& v);
    void addSession(const ListenEvent& event, const Vector& v);

    // сумма событий на момент m_longAt
    std::array<double, MusicSpace::kDim> m_long{};
    qint64 m_longAt = 0;
    std::array<double, MusicSpace::kDim> m_likes{};
    bool m_hasLong = false;

    Vector m_session{};
    int m_sessionLength = 0;
    qint64 m_sessionAt = 0;
};

#endif
