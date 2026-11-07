#ifndef EVALUATION_H
#define EVALUATION_H

#include "Recommender.h"
#include <QMap>
#include <QPair>

// Проверка модели на твоей истории. События идут по порядку, и модель
// сначала угадывает, потом учится, как в жизни: так она никогда не видит
// будущего. Оцениваются только последние testShare истории, остальное модель
// просто проходит. Ничего не пишет на диск: Recommender живёт в памяти
//
//   AUC: доля пар (дослушанный, пропущенный), где у дослушанного шанс выше;
//     0.5 это монетка, 1.0 идеально;
//   HitRate@10: настоящий трек в десятке лучших среди него и negatives
//     случайных песен, которые уже звучали;
//   NDCG@10: то же, но первое место стоит больше десятого, 1 / log2(1 + место)
class Evaluation {
public:
    struct Setup {
        // пусто: без карты музыки
        TasteVectors::VectorOf vectorOf;
        // пусто: без звука
        Recommender::SoundOf soundOf;
        double testShare = 0.2;
        int negatives = 99;
        // сколько дослушанных треков расставлять: дольше всего считается это
        int maxRanked = 2000;
        quint32 seed = 1;
    };
    struct Report {
        int events = 0;
        // прослушиваний в проверке с ясным итогом: дослушал или скип
        int scored = 0;
        qint64 testFrom = 0;
        double auc = 0.5;
        int ranked = 0;
        double hitRate = 0;
        double ndcg = 0;
        // источник: скипов и всего
        QMap<QString, QPair<int, int>> skips;
    };

    static Report run(const QList<ListenEvent>& events, const Setup& setup);
    // площадь под ROC через ранги (Манн-Уитни), равные оценки делят место
    static double auc(QList<QPair<double, bool>> scored);
};

#endif
