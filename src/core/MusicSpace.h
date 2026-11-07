#ifndef MUSIC_SPACE_H
#define MUSIC_SPACE_H

#include "KnowledgeBase.h"
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <optional>

// Пространство музыки: у каждого артиста точка, и рядом стоят те, кого слушают
// вместе. Строится из похожих артистов по Last.fm: граф, где ребро это
// похожесть, а координаты это главные собственные векторы нормированной
// матрицы смежности (спектральное вложение). По сути то же, что word2vec
// делает со словами, только в матричном виде (Levy и Goldberg, 2014), и граф
// в десятки тысяч артистов считается за доли секунды.
//
// Песня встаёт в точку своего артиста, сдвинутую к артистам её похожих треков:
// так у двух песен одного артиста разные места, а артист, которого нет в
// графе, всё равно находит место по своим похожим
class MusicSpace {
public:
    static constexpr int kDim = 32;
    using Vector = std::array<float, kDim>;

    // имя артиста и его соседи с похожестью; seed для повторяемости
    static MusicSpace build(const QList<QPair<QString, QList<KnowledgeBase::Neighbor>>>& graph, quint32 seed = 1);

    int size() const { return int(m_points.size()); }
    bool isEmpty() const { return m_points.isEmpty(); }
    const Vector* artist(const QString& artistKey) const;
    std::optional<Vector> song(const QVariantMap& track, const QList<KnowledgeBase::Neighbor>& similarTracks) const;
    // ближайшие артисты к точке: ключ и косинус
    QList<QPair<QString, double>> nearest(const Vector& v, int count) const;
    QString name(const QString& artistKey) const;

    static double cosine(const Vector& a, const Vector& b);
    // длина 1; нулевой вектор остаётся нулевым
    static Vector normalized(const Vector& v);

private:
    QHash<QString, int> m_index;
    QStringList m_keys;
    QStringList m_names;
    QVector<Vector> m_points;
};

#endif
