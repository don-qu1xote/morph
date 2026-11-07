#ifndef SUBSONIC_CATALOG_H
#define SUBSONIC_CATALOG_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Библиотека morph так, как её видит Subsonic: песни, альбомы, артисты и
// плейлисты с постоянными id. Телефон хранит у себя всё по id, поэтому id
// считаются из самой песни (songKey), а не из версии на площадке: когда MAIN
// меняет SoundCloud на Яндекс, скачанное на телефоне остаётся своим
class SubsonicCatalog {
public:
    struct Song {
        QString id;
        QString albumId;
        QString artistId;
        QString coverId;
        QVariantMap track;
        int number = 0;
        qint64 created = 0;
    };
    struct Album {
        QString id;
        QString name;
        QString artist;
        QString artistId;
        QString coverId;
        QStringList songs;
        qint64 created = 0;
        int duration = 0;
        bool mix = false;
    };
    struct Artist {
        QString id;
        QString name;
        QString coverId;
        QStringList albums;
    };
    struct Playlist {
        QString id;
        QString name;
        QString coverId;
        QStringList songs;
        int duration = 0;
        // MAIN: правки в нём это лайки, а не состав
        bool main = false;
    };
    struct PlaylistSource {
        QString name;
        QString coverUrl;
        QVariantList tracks;
    };

    static const QString kMainId;
    static const QString kMixArtistId;

    // seen: когда песня впервые попала на сервер (songKey: мс). Новые
    // дописываются, по этим датам телефон находит свежие альбомы и качает их
    void build(const QVariantList& library, const QList<PlaylistSource>& playlists, const QVariantList& mixes,
               QHash<QString, qint64>& seen, qint64 now);

    const Song* song(const QString& id) const;
    const Album* album(const QString& id) const;
    const Artist* artist(const QString& id) const;
    const Playlist* playlist(const QString& id) const;
    QString coverUrl(const QString& coverId) const { return m_covers.value(coverId); }

    // по названию, по артисту или новые первыми
    QList<const Album*> albums(const QString& order) const;
    // по имени, как в списке артистов на телефоне
    QList<const Artist*> artists() const;
    // MAIN первым, дальше свои плейлисты в порядке morph
    QList<const Playlist*> playlists() const;
    // все песни без миксов, MAIN сначала
    QList<const Song*> songs() const;
    int songCount() const { return int(m_songs.size()); }

    // всё, что не буква, собирается под #
    static QString indexLetter(const QString& name);

private:
    Song& addSong(const QString& id, const QVariantMap& track, const QString& albumId, qint64 created);
    QString albumFor(const QVariantMap& track, qint64 created);
    QString coverIdFor(const QString& url);

    QHash<QString, Song> m_songs;
    QHash<QString, Album> m_albums;
    QHash<QString, Artist> m_artists;
    QHash<QString, Playlist> m_playlists;
    QStringList m_playlistOrder;
    QStringList m_libraryOrder;
    QHash<QString, QString> m_covers;
};

#endif
