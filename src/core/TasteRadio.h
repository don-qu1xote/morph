#ifndef TASTE_RADIO_H
#define TASTE_RADIO_H

#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

class KnowledgeBase;
class MusicSpace;
class Recommender;

// Свои кандидаты для волны, без оглядки на площадку: что по Last.fm слушают
// вместе с тем, что ты сейчас дослушал. Из соседей выбираются те, что ближе
// к твоему вкусу в пространстве музыки. Где их сыграть, решается потом: трек
// уходит как запрос к YouTube, а отбор кандидатов в миксере находит ту же
// песню в Яндексе, если она там есть (там лучше звук)
class TasteRadio : public QObject {
    Q_OBJECT
public:
    TasteRadio(KnowledgeBase* knowledge, Recommender* recommender, QObject* parent = nullptr);
    // откуда брать пространство: оно пересобирается и меняется целиком
    void setSpace(std::function<std::shared_ptr<const MusicSpace>()> space) { m_space = std::move(space); }

    // по сидам, свежие первыми, до count готовых к игре треков с via = "Last.fm"
    void candidates(const QList<QVariantMap>& seeds, int count, std::function<void(const QVariantList&)> done);
    bool isAvailable() const;

    // оценка соседа: похожесть по Last.fm и близость к вкусу пополам
    static double score(double match, double cosine, bool hasVector);
    // песня по названию как запрос к YouTube с via = "Last.fm"; Яндекс находит отбор кандидатов
    static QVariantMap playable(const QString& artist, const QString& title, qint64 durationMs);

private:
    struct Pick {
        QString artist;
        QString title;
        qint64 durationMs = 0;
        double score = 0;
    };

    KnowledgeBase* knowledge;
    Recommender* recommender;
    std::function<std::shared_ptr<const MusicSpace>()> m_space;
};

#endif
