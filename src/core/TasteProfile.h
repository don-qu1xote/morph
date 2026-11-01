#ifndef TASTE_PROFILE_H
#define TASTE_PROFILE_H

#include "ListeningHistory.h"
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantList>

// Насколько ты любишь каждого артиста, от -1 до 1. Две части: лайки за всё
// время (вкус надолго) и недавние прослушивания, вес которых вдвое падает за
// 30 дней (что слушаешь сейчас). Дослушал плюс, ушёл в первые полминуты минус,
// дизлайк сильный минус. Итог tanh от суммы: много лайков не уводят в бесконечность
class TasteProfile {
public:
    // лайки за всё время, по одному на песню
    void setLikes(const QVariantList& likes);
    void add(const ListenEvent& event);

    double affinity(const QString& artistKey, qint64 now) const;
    double affinityOf(const QVariantMap& track, qint64 now) const;
    // сколько раз артиста включали
    int heard(const QString& artistKey) const;
    // знакомый: что-то у него дослушал или лайкнул
    bool known(const QString& artistKey) const;
    qint64 lastHeard(const QString& artistKey) const;
    // артисты по убыванию affinity, только любимые (больше нуля)
    QList<QPair<QString, double>> top(int count, qint64 now) const;
    QString name(const QString& artistKey) const { return m_artists.value(artistKey).name; }

private:
    struct Artist {
        QString name;
        // сумма весов на момент recentAt, дальше затухает
        double recent = 0;
        qint64 recentAt = 0;
        int likes = 0;
        int heard = 0;
        int finished = 0;
        qint64 lastHeard = 0;
    };
    static double decayed(const Artist& a, qint64 now);
    double raw(const Artist& a, qint64 now) const;

    QHash<QString, Artist> m_artists;
};

#endif
