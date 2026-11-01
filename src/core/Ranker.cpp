#include "Ranker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <cmath>

// насколько широко сэмплировать веса: 0 значит без разведки
static const double kExplore = 0.6;
// дальше точность не растёт, чтобы модель не застывала и успевала за вкусом
static const double kMaxPrecision = 100;

// С чего модель начинает, пока ничего о тебе не знает: любимые артисты и
// лайки вверх, часто пропускаемое и только что игравшее вниз. Точность у них
// выше, чем у новых признаков, поэтому первые же скипы их не перевернут
static const QHash<QString, QPair<double, double>>& priors() {
    static const QHash<QString, QPair<double, double>> p = {
        {"bias", {0.3, 1}},         {"affinity", {2.0, 4}},
        {"known", {0.3, 2}},        {"liked", {0.8, 2}},
        {"plays", {0.3, 2}},        {"skipRate", {-2.0, 2}},
        {"songRecent", {-1.0, 2}},  {"artistRecent", {-0.5, 2}},
        {"src:YouTube", {-0.2, 2}}, {"src:SoundCloud", {0.0, 2}},
        {"src:Yandex", {0.0, 2}},   {"len", {0.0, 2}},
        {"new", {0.0, 2}},
    };
    return p;
}

static double sigmoid(double z) {
    return 1.0 / (1.0 + std::exp(-z));
}

Ranker::Ranker() : m_random(QRandomGenerator::securelySeeded()) {}

Ranker::Weight Ranker::weight(const QString& feature) const {
    auto it = m_weights.constFind(feature);
    if (it != m_weights.constEnd()) return *it;
    auto prior = priors().constFind(feature);
    if (prior != priors().constEnd()) return {prior->first, prior->second};
    return {};
}

double Ranker::predict(const Features& x, bool explore) {
    double z = 0;
    for (const auto& [name, value] : x) {
        Weight w = weight(name);
        double m = w.mean;
        if (explore) {
            // нормальное распределение из двух равномерных (Бокс-Мюллер)
            double u1 = qMax(1e-12, m_random.generateDouble());
            double u2 = m_random.generateDouble();
            double eps = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
            m += kExplore * eps / std::sqrt(w.precision);
        }
        z += m * value;
    }
    return sigmoid(z);
}

// Один шаг: точность растёт на p(1-p) * x^2, среднее сдвигается на ошибку,
// делённую на точность. Чем увереннее вес, тем меньше его сдвигает
void Ranker::learn(const Features& x, double label, double weight) {
    double p = predict(x);
    for (const auto& [name, value] : x) {
        Weight w = this->weight(name);
        w.precision = qMin(kMaxPrecision, w.precision + weight * p * (1 - p) * value * value);
        w.mean += weight * (label - p) * value / w.precision;
        m_weights[name] = w;
    }
    ++m_updates;
}

double Ranker::mean(const QString& feature) const {
    return weight(feature).mean;
}

QByteArray Ranker::save() const {
    QJsonObject out;
    for (auto it = m_weights.cbegin(); it != m_weights.cend(); ++it)
        out.insert(it.key(), QJsonArray{it->mean, it->precision});
    return QJsonDocument(QJsonObject{{"updates", m_updates}, {"weights", out}}).toJson(QJsonDocument::Compact);
}

void Ranker::load(const QByteArray& data) {
    QJsonObject saved = QJsonDocument::fromJson(data).object();
    m_updates = saved["updates"].toInt();
    QJsonObject weights = saved["weights"].toObject();
    for (auto it = weights.begin(); it != weights.end(); ++it) {
        QJsonArray w = it.value().toArray();
        if (w.size() == 2) m_weights.insert(it.key(), {w[0].toDouble(), qMax(1.0, w[1].toDouble())});
    }
}
