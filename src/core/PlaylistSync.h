#ifndef PLAYLIST_SYNC_H
#define PLAYLIST_SYNC_H

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>

class Library;
class YandexService;

// Плейлисты morph настоящими плейлистами в Яндексе: тот же набор треков в
// том же порядке. Тогда они есть в приложении Яндекса на телефоне, а его
// рекомендации видят и их. Сверяется по одному плейлисту за раз. Треки с
// SoundCloud и YouTube попадают туда своей версией из каталога Яндекса,
// если она есть (связи MAIN или поиск от 0.85), иначе пропускаются
class PlaylistSync : public QObject {
    Q_OBJECT
public:
    PlaylistSync(YandexService* yandex, Library* library, const QString& path, QObject* parent = nullptr);

    void setEnabled(bool enabled);
    // все плейлисты morph, как их отдаёт MorphSettings.getPlaylists()
    void sync(const QVariantMap& playlists);
    // переименовали в morph: в Яндексе тот же плейлист, только с новым именем
    void renamed(const QString& oldName, const QString& newName);

signals:
    // onYandex из total треков нашлись в Яндексе
    void synced(const QString& name, int onYandex, int total);

private:
    void next();
    void syncOne(const QString& name, const QVariantList& tracks);
    void resolve(const QVariantList& tracks, std::function<void(const QStringList& ids)> done);
    void apply(const QString& name, const QString& kind, const QStringList& tracks, int total, bool retried);
    void load();
    void save() const;

    YandexService* yandex;
    Library* library;
    QString m_path;
    bool m_enabled = false;
    bool m_busy = false;
    QVariantMap m_playlists;
    QStringList m_queue;
    // имя плейлиста в morph: kind в Яндексе
    QHash<QString, QString> m_kinds;
    // TrackMatch::songKey: id в Яндексе, пусто если не нашлось
    QHash<QString, QString> m_found;
};

#endif
