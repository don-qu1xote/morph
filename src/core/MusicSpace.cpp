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

// столбцы n на k по очереди: ортонормировать Грамом-Шмидтом
static void orthonormalize(QVector<double>& x, int n, int k) {
    for (int j = 0; j < k; ++j) {
        double* col = x.data() + qsizetype(j) * n;
        for (int prev = 0; prev < j; ++prev) {
            const double* p = x.constData() + qsizetype(prev) * n;
            double dot = 0;
            for (int i = 0; i < n; ++i) dot += col[i] * p[i];
            for (int i = 0; i < n; ++i) col[i] -= dot * p[i];
        }
        double norm = 0;
        for (int i = 0; i < n; ++i) norm += col[i] * col[i];
        norm = std::sqrt(norm);
        if (norm < 1e-12) continue;
        for (int i = 0; i < n; ++i) col[i] /= norm;
    }
}

MusicSpace MusicSpace::build(const QList<QPair<QString, QList<KnowledgeBase::Neighbor>>>& graph, quint32 seed) {
    MusicSpace space;
    QVector<QHash<int, double>> adjacency;
    auto node = [&](const QString& name) {
        QString key = artistKeyOf(name);
        if (key.isEmpty()) return -1;
        auto it = space.m_index.constFind(key);
        if (it != space.m_index.cend()) return it.value();
        int id = int(space.m_keys.size());
        space.m_index.insert(key, id);
        space.m_keys.append(key);
        space.m_names.append(name);
        adjacency.append(QHash<int, double>());
        return id;
    };
    for (const auto& [source, neighbors] : graph) {
        int s = node(source);
        if (s < 0) continue;
        for (const KnowledgeBase::Neighbor& n : neighbors) {
            int t = node(n.artist);
            if (t < 0 || t == s) continue;
            double w = qBound(0.0, n.match, 1.0);
            if (w <= 0) continue;
            double& st = adjacency[s][t];
            st = qMax(st, w);
            adjacency[t][s] = st;
        }
    }

    const int n = int(space.m_keys.size());
    if (n < 3) {
        space = MusicSpace();
        return space;
    }

    // S = D^-1/2 (A + I) D^-1/2: петля на каждом узле держит одиноких артистов на месте
    QVector<double> scale(n);
    for (int i = 0; i < n; ++i) {
        double degree = 1;
        for (double w : std::as_const(adjacency[i])) degree += w;
        scale[i] = 1 / std::sqrt(degree);
    }
    auto multiply = [&](const QVector<double>& x, QVector<double>& y, int k) {
        y.fill(0, qsizetype(n) * k);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < k; ++j) {
                y[qsizetype(j) * n + i] += x[qsizetype(j) * n + i] * scale[i] * scale[i];
            }
            for (auto it = adjacency[i].cbegin(); it != adjacency[i].cend(); ++it) {
                double w = it.value() * scale[i] * scale[it.key()];
                for (int j = 0; j < k; ++j) y[qsizetype(j) * n + i] += w * x[qsizetype(j) * n + it.key()];
            }
        }
    };

    const int k = qMin(n, kDim + 1 + kOversample);
    QRandomGenerator random(seed);
    QVector<double> x(qsizetype(n) * k);
    for (double& value : x) value = random.generateDouble() * 2 - 1;
    orthonormalize(x, n, k);
    QVector<double> y;
    for (int iter = 0; iter < kIterations; ++iter) {
        multiply(x, y, k);
        x.swap(y);
        orthonormalize(x, n, k);
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

    // Первый вектор пропускаем: у связного графа он про число соседей, а не про
    // музыку. Остальные весят как lambda^2 (диффузионные координаты): мелкие
    // собственные числа это шум внутри компании, без веса они разводили бы похожих
    const int dims = qMin(kDim, k - 1);
    space.m_points.resize(n);
    for (int i = 0; i < n; ++i) {
        Vector point{};
        for (int d = 0; d < dims; ++d) {
            int col = order[d + 1];
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
