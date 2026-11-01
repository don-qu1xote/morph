#ifndef RANKER_H
#define RANKER_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QRandomGenerator>
#include <QString>

// Модель «дослушаешь ли трек»: логистическая регрессия p = sigmoid(w * x), которая
// учится после каждого трека. У каждого веса не одно число, а среднее и
// точность (байесовская логрегрессия с диагональной гауссовой апостериорной,
// как у Chapelle и Li, 2011). Точность растёт, когда признак встречается
// и модель в нём уверена, поэтому редкие признаки учатся быстро, а частые
// не дёргаются от одного скипа.
//
// Для разведки веса не берутся средними, а сэмплируются: w = m + 0.6 * eps / sqrt(q)
// (Thompson sampling). Про незнакомое модель знает мало, его разброс шире,
// и иногда оно выигрывает. По мере того как модель узнаёт тебя, разброс
// сужается и разведки становится меньше сама собой
class Ranker {
public:
    // признак: имя и значение, имена вида "affinity", "src:YouTube", "a:<артист>"
    using Features = QList<QPair<QString, double>>;

    Ranker();

    // вероятность дослушать; explore: со случайными весами по разбросу
    double predict(const Features& x, bool explore = false);
    // label от 0 до 1, weight: насколько этот пример важен
    void learn(const Features& x, double label, double weight = 1.0);

    void setSeed(quint32 seed) { m_random.seed(seed); }
    double mean(const QString& feature) const;
    int updates() const { return m_updates; }

    // {имя: [среднее, точность]}
    QByteArray save() const;
    void load(const QByteArray& data);

private:
    struct Weight {
        double mean = 0;
        double precision = 1;
    };
    Weight weight(const QString& feature) const;

    QHash<QString, Weight> m_weights;
    QRandomGenerator m_random;
    int m_updates = 0;
};

#endif
