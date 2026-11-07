#ifndef PHONE_MIXES_H
#define PHONE_MIXES_H

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>

// Миксы для телефона раз в день: Daily Mix каждый день, Discover Weekly и
// Release Radar раз в неделю. Каждый день это новые альбомы на сервере, и
// Amperfy с автозагрузкой новых альбомов забирает их, пока телефон дома в той
// же сети. Старые уходят через три дня (недельные через восемь), чтобы
// телефон не копил всё подряд
class PhoneMixes : public QObject {
    Q_OBJECT
public:
    // cards и build от MorphMixes, ответы приходят в collected и skipped
    PhoneMixes(std::function<QVariantList()> cards, std::function<void(const QString&)> build, const QString& path,
               QObject* parent = nullptr);
    ~PhoneMixes() override;

    // id, card, name, created (мс), coverUrl, tracks
    QVariantList mixes() const { return m_mixes; }
    void setEnabled(bool enabled);
    // собрать сегодняшние, если их ещё нет
    void check();
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

    static constexpr int kTracksPerMix = 30;

public slots:
    void collected(const QString& name, const QString& coverUrl, const QVariantList& tracks);
    void skipped();

signals:
    void changed();
    // собраны новые: их треки стоит заранее скачать на мак
    void built(const QVariantList& tracks);

private:
    void buildNext();
    void finishRound();
    void save() const;

    std::function<QVariantList()> m_cards;
    std::function<void(const QString&)> m_build;
    QString m_path;
    std::function<QDateTime()> m_clock;
    QVariantList m_mixes;
    QString m_day;
    QString m_week;
    // текущий круг сборки
    QStringList m_queue;
    QString m_building;
    QString m_roundDay;
    QString m_roundWeek;
    bool m_roundWeekly = false;
    QVariantList m_fresh;
    QTimer m_timer;
    QTimer m_timeout;
};

#endif
