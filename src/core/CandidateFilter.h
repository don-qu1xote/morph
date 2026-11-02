#ifndef CANDIDATE_FILTER_H
#define CANDIDATE_FILTER_H

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <functional>

class YandexService;

// Отбор кандидатов с SoundCloud и YouTube до ранжирования. Яндекс присылает
// треки, которые его рекомендации уже отобрали под тебя, а эти два сервиса
// присылают сырой поиск похожего: ремиксы, перезаливы, биты, часовые миксы,
// а в поле артиста часто тот, кто загрузил. Без отбора такие треки всегда
// проигрывали бы яндексовским, и не потому что хуже.
//
// Что делается с каждым:
//   1. мусор выкидывается (rejectReason);
//   2. подпись приводится в порядок (TrackMatch::cleaned);
//   3. трек ищется в каталоге Яндекса. Нашёлся и отдаётся, дальше играет
//      яндексовская версия; нашёлся, но убран, остаётся свой, но с настоящими
//      артистом и названием. Ответы каталога кэшируются на диске.
// У каждого вышедшего трека поле via: площадка, которая его посоветовала
class CandidateFilter : public QObject {
    Q_OBJECT
public:
    CandidateFilter(YandexService* yandex, const QString& cachePath, QObject* parent = nullptr);

    // done приходит всегда, и тогда, когда Яндекс не ответил
    void process(const QVariantList& tracks, const QString& origin, std::function<void(const QVariantList&)> done);

    // Почему трек не песня: другая версия, бит, нарезка, превью, микс на час.
    // Пусто, если песня
    static QString rejectReason(const QVariantMap& track);

private:
    struct Entry {
        QVariantMap yandex; // пусто, если в каталоге нет
        qint64 checkedAt = 0;
    };
    QVariantMap resolved(const QVariantMap& track, const QString& origin) const;
    void load();
    void save() const;

    YandexService* yandex;
    QString m_cachePath;
    // TrackMatch::key чистой подписи: что нашлось в Яндексе
    QHash<QString, Entry> m_catalog;
};

#endif
