#ifndef LIBRARY_H
#define LIBRARY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>

// MAIN: один плейлист на все площадки. Это те же лайки, только одна песня
// здесь одна запись, сколько бы её версий ни было лайкнуто на Яндексе,
// SoundCloud и YouTube. У записи есть источники: лайкнутые версии и найденные
// в каталоге Яндекса (связи). Связи хранятся на диске, состав берётся из
// лайков, поэтому разъехаться им не с чем
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit Library(const QString& path, QObject* parent = nullptr);

    // false для трека, который сервис знает, но не отдаёт
    void setAvailability(std::function<bool(const QVariantMap&)> available) { m_available = std::move(available); }
    void syncLikes(const QVariantList& likes);

    int count() const { return m_order.size(); }
    // Треки для экрана MAIN, новые сверху. Каждый это лайкнутая версия, которая
    // играет надёжнее остальных, плюс alsoOn (где ещё есть песня) и songKey
    Q_INVOKABLE QVariantList tracks() const;
    // сервисы, где есть песня с этим треком, начиная с его собственного
    Q_INVOKABLE QStringList sourcesOf(const QString& trackId) const;

    bool contains(const QString& trackId) const { return m_keyOf.contains(trackId); }
    // сам трек, если он есть в MAIN среди лайков или связей
    QVariantMap trackById(const QString& trackId) const;
    // Другие версии той же песни для фолбэка: лайкнутые и связи, без сервиса
    // самого трека и без версий, которые сервис не отдаёт
    QList<QVariantMap> alternatives(const QString& trackId) const;
    // id песни в Яндексе: лайкнутый или найденный. Связь живёт и после того,
    // как песню убрали из MAIN, чтобы снять лайк и там
    QString yandexId(const QString& songKey) const;

    // Песня без версии в Яндексе, которую давно не искали; пусто, если таких нет.
    // По найденной связи волна, лайки и фолбэк работают и для SoundCloud
    QString nextToLink() const;
    QVariantMap representative(const QString& songKey) const;
    void markLinkTried(const QString& songKey);
    void link(const QString& songKey, const QVariantMap& track);

signals:
    void changed();

private:
    struct Song {
        QList<QVariantMap> liked; // в порядке лайков, новые первыми
        QHash<QString, QVariantMap> linked;
    };

    int rank(const QVariantMap& track) const;
    QVariantMap best(const Song& song) const;
    QList<QVariantMap> allSources(const Song& song) const;
    void load();
    void save() const;
    static QVariantMap slim(const QVariantMap& track);

    QString m_path;
    std::function<bool(const QVariantMap&)> m_available;
    QHash<QString, Song> m_songs;
    QStringList m_order;
    QHash<QString, QString> m_keyOf;
    // songKey: сервис: трек
    QHash<QString, QHash<QString, QVariantMap>> m_links;
    // songKey: когда искали в последний раз, секунды
    QHash<QString, qint64> m_tried;
};

#endif
