#include "MusicSpace.h"
#include "TrackMatch.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <numeric>

// столько раз умножаем на матрицу: восьми хватает, чтобы главные векторы отделились
static const int kIterations = 8;
// запас столбцов сверх нужных: с ним старшие собственные векторы точнее
static const int kOversample = 8;
// доля средней степени, которая добавляется каждому артисту, см. build
static const double kRegularization = 1.0;

static QString artistKeyOf(const QString& name) {
    return TrackMatch::artistKey(QVariantMap{{"artist", name}});
}

// Якоби для маленькой симметричной матрицы k на k: собственные числа в a[i][i],
// векторы по столбцам v
static void jacobi(QVector<double>& a, QVector<double>& v, int k) {
    v.fill(0, k * k);
    for (int i = 0; i < k; ++i) v[i * k + i] = 1;
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0;
        for (int p = 0; p < k; ++p) {
            for (int q = p + 1; q < k; ++q) off += a[p * k + q] * a[p * k + q];
        }
        if (off < 1e-20) return;
        for (int p = 0; p < k; ++p) {
            for (int q = p + 1; q < k; ++q) {
                double apq = a[p * k + q];
                if (std::abs(apq) < 1e-15) continue;
                double theta = (a[q * k + q] - a[p * k + p]) / (2 * apq);
                double t = (theta >= 0 ? 1 : -1) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1);
                double s = t * c;
                for (int r = 0; r < k; ++r) {
                    double arp = a[r * k + p];
                    double arq = a[r * k + q];
                    a[r * k + p] = c * arp - s * arq;
                    a[r * k + q] = s * arp + c * arq;
                }
                for (int r = 0; r < k; ++r) {
                    double apr = a[p * k + r];
                    double aqr = a[q * k + r];
                    a[p * k + r] = c * apr - s * aqr;
                    a[q * k + r] = s * apr + c * aqr;
                }
                for (int r = 0; r < k; ++r) {
                    double vrp = v[r * k + p];
                    double vrq = v[r * k + q];
                    v[r * k + p] = c * vrp - s * vrq;
                    v[r * k + q] = s * vrp + c * vrq;
                }
            }
        }
    }
}

// Столбцы n на k по очереди: убрать направление trivial и ортонормировать
// Грамом-Шмидтом. Столбец, от которого ничего не осталось, обнуляется, иначе
// остаток округления попал бы в поворот Рэлея-Ритца как настоящий вектор
static void orthonormalize(QVector<double>& x, int n, int k, const QVector<double>& trivial) {
    for (int j = 0; j < k; ++j) {
        double* col = x.data() + qsizetype(j) * n;
        auto remove = [&](const double* p) {
            double dot = 0;
            for (int i = 0; i < n; ++i) dot += col[i] * p[i];
            for (int i = 0; i < n; ++i) col[i] -= dot * p[i];
        };
        remove(trivial.constData());
        for (int prev = 0; prev < j; ++prev) remove(x.constData() + qsizetype(prev) * n);
        double norm = 0;
        for (int i = 0; i < n; ++i) norm += col[i] * col[i];
        norm = std::sqrt(norm);
        if (norm < 1e-12) {
            std::fill(col, col + n, 0.0);
            continue;
        }
        for (int i = 0; i < n; ++i) col[i] /= norm;
    }
}

MusicSpace MusicSpace::build(const QList<QPair<QString, QList<KnowledgeBase::Neighbor>>>& graph, quint32 seed) {
    MusicSpace space;
    QVector<QHash<int, double>> adjacency;
    auto node = [&](const QString& key, const QString& name) {
        auto it = space.m_index.constFind(key);
        if (it != space.m_index.cend()) return it.value();
        int id = int(space.m_keys.size());
        space.m_index.insert(key, id);
        space.m_keys.append(key);
        space.m_names.append(name);
        adjacency.append(QHash<int, double>());
        return id;
    };
    // Узел появляется только с первым ребром. Артист без похожих (Last.fm его
    // не знает) ничего не говорит о соседстве, а одинокая точка дала бы ещё
    // одно собственное число 1 и забрала бы измерение у настоящих компаний
    for (const auto& [source, neighbors] : graph) {
        QString sourceKey = artistKeyOf(source);
        if (sourceKey.isEmpty()) continue;
        for (const KnowledgeBase::Neighbor& n : neighbors) {
            double w = qBound(0.0, n.match, 1.0);
            QString targetKey = artistKeyOf(n.artist);
            if (w <= 0 || targetKey.isEmpty() || targetKey == sourceKey) continue;
            int s = node(sourceKey, source);
            int t = node(targetKey, n.artist);
            double& st = adjacency[s][t];
            st = qMax(st, w);
            adjacency[t][s] = st;
        }
    }

    const int n = int(space.m_keys.size());
    if (n < 3) return {};

    // Рёбра по порядку номеров: от порядка сложения зависит округление, а QHash
    // в каждом запуске обходит ключи по-своему. С порядком карта из одних и тех
    // же ответов Last.fm получается одной и той же
    QVector<int> first(n + 1, 0);
    QVector<int> to;
    QVector<double> weight;
    QVector<double> degree(n);
    double total = 0;
    for (int i = 0; i < n; ++i) {
        QList<int> keys = adjacency[i].keys();
        std::sort(keys.begin(), keys.end());
        // петля на каждом узле: артист с одним соседом не скачет от итерации к итерации
        degree[i] = 1;
        for (int j : std::as_const(keys)) {
            double w = adjacency[i].value(j);
            to.append(j);
            weight.append(w);
            degree[i] += w;
        }
        first[i + 1] = int(to.size());
        total += degree[i];
    }

    // Регуляризация (Amini и др., 2013): к каждой степени средняя степень tau, а
    // все артисты слабо связаны друг с другом весом tau / n. Граф становится
    // связным, и мелкие острова из пары малоизвестных артистов не забирают
    // главные измерения у больших компаний (Zhang и Rohe, 2018).
    // S = D^-1/2 (A + I + tau/n) D^-1/2, у неё ровно известный вектор sqrt(d)
    // с числом 1: он про то, сколько у артиста соседей, а не про музыку
    const double tau = kRegularization * total / n;
    QVector<double> scale(n);
    QVector<double> trivial(n);
    double length = 0;
    for (int i = 0; i < n; ++i) {
        double d = degree[i] + tau;
        scale[i] = 1 / std::sqrt(d);
        trivial[i] = std::sqrt(d);
        length += d;
    }
    for (double& t : trivial) t /= std::sqrt(length);

    QVector<double> z(n);
    auto multiply = [&](const QVector<double>& x, QVector<double>& y, int k) {
        y.resize(qsizetype(n) * k);
        for (int j = 0; j < k; ++j) {
            const double* in = x.constData() + qsizetype(j) * n;
            double* out = y.data() + qsizetype(j) * n;
            double everyone = 0;
            for (int i = 0; i < n; ++i) {
                z[i] = in[i] * scale[i];
                everyone += z[i];
            }
            everyone *= tau / n;
            for (int i = 0; i < n; ++i) {
                double sum = z[i] + everyone;
                for (int e = first[i]; e < first[i + 1]; ++e) sum += weight[e] * z[to[e]];
                out[i] = sum * scale[i];
            }
        }
    };

    // Вектор sqrt(d) убираем из подпространства на каждом шаге, а не ищем и
    // выкидываем потом. Когда у S несколько чисел 1, «первый» среди них выбирал
    // шум округления, и карта зависела от процессора и запуска
    const int k = qMin(n - 1, kDim + kOversample);
    QRandomGenerator random(seed);
    QVector<double> x(qsizetype(n) * k);
    for (double& value : x) value = random.generateDouble() * 2 - 1;
    orthonormalize(x, n, k, trivial);
    QVector<double> y;
    for (int iter = 0; iter < kIterations; ++iter) {
        multiply(x, y, k);
        x.swap(y);
        orthonormalize(x, n, k, trivial);
    }

    // Рэлей-Ритц: B = X^T S X, его собственные векторы поворачивают X в собственные S
    multiply(x, y, k);
    QVector<double> b(k * k);
    for (int p = 0; p < k; ++p) {
        for (int q = 0; q < k; ++q) {
            double dot = 0;
            for (int i = 0; i < n; ++i) dot += x[qsizetype(p) * n + i] * y[qsizetype(q) * n + i];
            b[p * k + q] = dot;
        }
    }
    for (int p = 0; p < k; ++p) {
        for (int q = p + 1; q < k; ++q) b[p * k + q] = b[q * k + p] = (b[p * k + q] + b[q * k + p]) / 2;
    }
    QVector<double> v;
    jacobi(b, v, k);
    QVector<int> order(k);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int l, int r) { return b[l * k + l] > b[r * k + r]; });

    // Каждая координата весит lambda^2 (диффузионные координаты): мелкие
    // собственные числа это шум внутри компании, без веса они разводили бы похожих
    const int dims = qMin(kDim, k);
    space.m_points.resize(n);
    for (int i = 0; i < n; ++i) {
        Vector point{};
        for (int d = 0; d < dims; ++d) {
            int col = order[d];
            double lambda = qMax(0.0, b[col * k + col]);
            double value = 0;
            for (int j = 0; j < k; ++j) value += x[qsizetype(j) * n + i] * v[j * k + col];
            point[d] = float(value * lambda * lambda);
        }
        space.m_points[i] = normalized(point);
    }
    return space;
}

const MusicSpace::Vector* MusicSpace::artist(const QString& artistKey) const {
    auto it = m_index.constFind(artistKey);
    return it == m_index.cend() ? nullptr : &m_points[it.value()];
}

QString MusicSpace::name(const QString& artistKey) const {
    auto it = m_index.constFind(artistKey);
    return it == m_index.cend() ? QString() : m_names[it.value()];
}

std::optional<MusicSpace::Vector> MusicSpace::song(const QVariantMap& track,
                                                   const QList<KnowledgeBase::Neighbor>& similarTracks) const {
    if (isEmpty()) return std::nullopt;
    const Vector* own = artist(TrackMatch::artistKey(track));
    Vector near{};
    double weight = 0;
    for (const KnowledgeBase::Neighbor& n : similarTracks) {
        const Vector* p = artist(artistKeyOf(n.artist));
        double w = qMax(0.05, n.match);
        if (!p) continue;
        for (int d = 0; d < kDim; ++d) near[d] += float(w * (*p)[d]);
        weight += w;
    }
    if (!own && weight == 0) return std::nullopt;
    Vector sum{};
    // у своего артиста вес вдвое больше, чем у всех похожих вместе
    double nearShare = own ? 0.5 : 1.0;
    for (int d = 0; d < kDim; ++d) {
        if (own) sum[d] += (*own)[d];
        if (weight > 0) sum[d] += float(nearShare * near[d] / weight);
    }
    return normalized(sum);
}

QList<QPair<QString, double>> MusicSpace::nearest(const Vector& v, int count) const {
    QList<QPair<QString, double>> scored;
    scored.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) scored.append({m_keys[i], cosine(v, m_points[i])});
    std::partial_sort(scored.begin(), scored.begin() + qMin(count, int(scored.size())), scored.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
    return scored.mid(0, count);
}

double MusicSpace::cosine(const Vector& a, const Vector& b) {
    double dot = 0;
    double na = 0;
    double nb = 0;
    for (int d = 0; d < kDim; ++d) {
        dot += double(a[d]) * b[d];
        na += double(a[d]) * a[d];
        nb += double(b[d]) * b[d];
    }
    if (na <= 0 || nb <= 0) return 0;
    return dot / std::sqrt(na * nb);
}

MusicSpace::Vector MusicSpace::normalized(const Vector& v) {
    double norm = 0;
    for (float f : v) norm += double(f) * f;
    if (norm <= 0) return v;
    norm = std::sqrt(norm);
    Vector out;
    for (int d = 0; d < kDim; ++d) out[d] = float(v[d] / norm);
    return out;
}
