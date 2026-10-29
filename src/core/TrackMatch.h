#ifndef TRACK_MATCH_H
#define TRACK_MATCH_H

#include <QString>
#include <QVariantMap>

// Одна песня на разных площадках подписана по-разному, а поиск выдаёт рядом
// лайвы, каверы и чужие треки с тем же словом в названии. Тут всё, что нужно,
// чтобы понять, та ли это песня. Треки это словари с полями artist, title и
// durationMs, как у TrackData::toVariantMap
namespace TrackMatch {

// ключ для поиска повторов: первый артист и название без скобок
QString key(const QVariantMap& track);

// насколько candidate похож на wanted, от 0 до 1. Длительность 0 значит неизвестна
double score(const QVariantMap& wanted, const QVariantMap& candidate);

// строка импорта "Артист - Название" или просто "Название"
QVariantMap fromQuery(const QString& query);

// обратно в строку для поиска
QString query(const QVariantMap& track);

} // namespace TrackMatch

#endif
