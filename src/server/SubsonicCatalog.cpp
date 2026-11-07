#include "SubsonicCatalog.h"
#include "../core/TrackMatch.h"
#include <QCryptographicHash>
#include <algorithm>

const QString SubsonicCatalog::kMainId = QStringLiteral("plmain");
const QString SubsonicCatalog::kMixArtistId = QStringLiteral("armorph");

static QString hashId(const QString& prefix, const QString& text) {
    QByteArray hex = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex().left(16);
    return prefix + QString::fromLatin1(hex);
}

static int seconds(const QVariantMap& track) {
    return int((track.value("durationMs").toLongLong() + 500) / 1000);
}

static QString songKeyOf(const QVariantMap& track) {
    QString key = track.value("songKey").toString();
    return key.isEmpty() ? TrackMatch::songKey(track) : key;
}

// Площадки подставляют вместо альбома своё имя. Такой трек идёт синглом,
// иначе у половины артистов был бы один альбом «YouTube Music» на всё подряд
static bool isPlaceholderAlbum(const QVariantMap& track) {
    QString title = track.value("album").toString().trimmed().toLower();
    return title.isEmpty() || title == "youtube music" || title == "youtube" || title == "soundcloud" ||
           title == track.value("service").toString().toLower();
}

QString SubsonicCatalog::indexLetter(const QString& name) {
    for (const QChar c : name) {
        if (c.isLetter()) return QString(c.toUpper());
        if (!c.isSpace()) break;
    }
    return QStringLiteral("#");
}

QString SubsonicCatalog::coverIdFor(const QString& url) {
    if (url.isEmpty()) return {};
    QString id = hashId("c", url);
    m_covers.insert(id, url);
    return id;
}

QString SubsonicCatalog::albumFor(const QVariantMap& track, qint64 created) {
    QString artistKey = TrackMatch::artistKey(track);
    bool single = isPlaceholderAlbum(track);
    QString title = single ? track.value("title").toString() : track.value("album").toString();
    QString key = artistKey + '\n' + (single ? "single:" + songKeyOf(track) : title.toLower().simplified());
    QString id = hashId("al", key);
    if (m_albums.contains(id)) return id;

    QString artistId = hashId("ar", artistKey);
    Artist& artist = m_artists[artistId];
    if (artist.id.isEmpty()) {
        artist.id = artistId;
        artist.name = TrackMatch::mainArtist(track);
    }
    artist.albums.append(id);

    Album album;
    album.id = id;
    album.name = title;
    album.artist = artist.name;
    album.artistId = artistId;
    album.created = created;
    m_albums.insert(id, album);
    return id;
}

SubsonicCatalog::Song& SubsonicCatalog::addSong(const QString& id, const QVariantMap& track, const QString& albumId,
                                                qint64 created) {
    Album& album = m_albums[albumId];
    album.songs.append(id);
    album.duration += seconds(track);
    album.created = qMax(album.created, created);

    Song song;
    song.id = id;
    song.albumId = albumId;
    song.track = track;
    song.number = int(album.songs.size());
    song.created = created;
    song.coverId = coverIdFor(track.value("coverUrl").toString());
    QString artistId = hashId("ar", TrackMatch::artistKey(track));
    if (m_artists.contains(artistId)) song.artistId = artistId;
    if (album.coverId.isEmpty()) album.coverId = song.coverId;
    if (!album.artistId.isEmpty() && m_artists[album.artistId].coverId.isEmpty()) {
        m_artists[album.artistId].coverId = song.coverId;
    }
    return m_songs[id] = song;
}

void SubsonicCatalog::build(const QVariantList& library, const QList<PlaylistSource>& playlists,
                            const QVariantList& mixes, QHash<QString, qint64>& seen, qint64 now) {
    m_songs.clear();
    m_albums.clear();
    m_artists.clear();
    m_playlists.clear();
    m_playlistOrder.clear();
    m_libraryOrder.clear();
    m_covers.clear();

    // В первый раз даты расставляются по порядку MAIN, чтобы новые были выше.
    // Дальше всё, чего сервер ещё не видел, получает текущее время
    const bool firstRun = seen.isEmpty();
    qint64 order = 0;
    auto createdFor = [&](const QString& key) {
        auto it = seen.find(key);
        if (it == seen.end()) it = seen.insert(key, firstRun ? now - order * 1000 : now);
        ++order;
        return it.value();
    };
    auto librarySong = [&](const QVariantMap& track) {
        QString key = songKeyOf(track);
        QString id = hashId("s", key);
        if (!m_songs.contains(id)) {
            qint64 created = createdFor(key);
            addSong(id, track, albumFor(track, created), created);
            m_libraryOrder.append(id);
        }
        return id;
    };

    Playlist main;
    main.id = kMainId;
    main.name = QStringLiteral("MAIN");
    main.main = true;
    for (const QVariant& v : library) {
        QString id = librarySong(v.toMap());
        if (main.songs.contains(id)) continue;
        main.songs.append(id);
        main.duration += seconds(m_songs[id].track);
        if (main.coverId.isEmpty()) main.coverId = m_songs[id].coverId;
    }
    m_playlists.insert(main.id, main);
    m_playlistOrder.append(main.id);

    for (const PlaylistSource& source : playlists) {
        Playlist pl;
        pl.id = hashId("pl", source.name);
        pl.name = source.name;
        pl.coverId = coverIdFor(source.coverUrl);
        for (const QVariant& v : source.tracks) {
            QString id = librarySong(v.toMap());
            pl.songs.append(id);
            pl.duration += seconds(m_songs[id].track);
            if (pl.coverId.isEmpty()) pl.coverId = m_songs[id].coverId;
        }
        m_playlists.insert(pl.id, pl);
        m_playlistOrder.append(pl.id);
    }

    // Микс это альбом артиста morph со своими песнями. Свои id нужны, чтобы
    // телефон считал его новым альбомом и скачал целиком, а не перетаскивал
    // песни из их настоящих альбомов
    if (!mixes.isEmpty()) {
        Artist& morph = m_artists[kMixArtistId];
        morph.id = kMixArtistId;
        morph.name = QStringLiteral("morph");
    }
    for (const QVariant& m : mixes) {
        QVariantMap mix = m.toMap();
        QString mixId = mix.value("id").toString();
        qint64 created = mix.value("created").toLongLong();
        Album album;
        album.id = hashId("mx", mixId);
        album.name = mix.value("name").toString();
        album.artist = QStringLiteral("morph");
        album.artistId = kMixArtistId;
        album.created = created;
        album.mix = true;
        album.coverId = coverIdFor(mix.value("coverUrl").toString());
        m_albums.insert(album.id, album);
        m_artists[kMixArtistId].albums.append(album.id);
        for (const QVariant& v : mix.value("tracks").toList()) {
            QVariantMap track = v.toMap();
            QString id = hashId("m", mixId + '\n' + songKeyOf(track));
            if (!m_songs.contains(id)) addSong(id, track, album.id, created);
        }
        if (m_artists[kMixArtistId].coverId.isEmpty()) m_artists[kMixArtistId].coverId = m_albums[album.id].coverId;
    }
}

const SubsonicCatalog::Song* SubsonicCatalog::song(const QString& id) const {
    auto it = m_songs.constFind(id);
    return it == m_songs.cend() ? nullptr : &it.value();
}

const SubsonicCatalog::Album* SubsonicCatalog::album(const QString& id) const {
    auto it = m_albums.constFind(id);
    return it == m_albums.cend() ? nullptr : &it.value();
}

const SubsonicCatalog::Artist* SubsonicCatalog::artist(const QString& id) const {
    auto it = m_artists.constFind(id);
    return it == m_artists.cend() ? nullptr : &it.value();
}

const SubsonicCatalog::Playlist* SubsonicCatalog::playlist(const QString& id) const {
    auto it = m_playlists.constFind(id);
    return it == m_playlists.cend() ? nullptr : &it.value();
}

// Телефон листает альбомы страницами по 500 и параллельно, поэтому порядок
// обязан быть одним и тем же от запроса к запросу: при равенстве решает id
QList<const SubsonicCatalog::Album*> SubsonicCatalog::albums(const QString& order) const {
    QList<const Album*> out;
    out.reserve(m_albums.size());
    for (const Album& a : m_albums) out.append(&a);
    auto byId = [](const Album* a, const Album* b) { return a->id < b->id; };
    if (order == "newest") {
        std::sort(out.begin(), out.end(), [&](const Album* a, const Album* b) {
            return a->created != b->created ? a->created > b->created : byId(a, b);
        });
    } else if (order == "alphabeticalByArtist") {
        std::sort(out.begin(), out.end(), [&](const Album* a, const Album* b) {
            int c = a->artist.compare(b->artist, Qt::CaseInsensitive);
            if (c == 0) c = a->name.compare(b->name, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : byId(a, b);
        });
    } else {
        std::sort(out.begin(), out.end(), [&](const Album* a, const Album* b) {
            int c = a->name.compare(b->name, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : byId(a, b);
        });
    }
    return out;
}

QList<const SubsonicCatalog::Artist*> SubsonicCatalog::artists() const {
    QList<const Artist*> out;
    out.reserve(m_artists.size());
    for (const Artist& a : m_artists) {
        if (!a.albums.isEmpty()) out.append(&a);
    }
    std::sort(out.begin(), out.end(), [](const Artist* a, const Artist* b) {
        int c = a->name.compare(b->name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a->id < b->id;
    });
    return out;
}

QList<const SubsonicCatalog::Playlist*> SubsonicCatalog::playlists() const {
    QList<const Playlist*> out;
    for (const QString& id : m_playlistOrder) out.append(&m_playlists.find(id).value());
    return out;
}

QList<const SubsonicCatalog::Song*> SubsonicCatalog::songs() const {
    QList<const Song*> out;
    out.reserve(m_libraryOrder.size());
    for (const QString& id : m_libraryOrder) out.append(&m_songs.find(id).value());
    return out;
}
