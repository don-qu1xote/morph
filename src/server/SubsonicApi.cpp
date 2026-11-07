#include "SubsonicApi.h"
#include "../utils/BackgroundWriter.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QXmlStreamWriter>
#include <algorithm>
#include <vector>

const char* SubsonicApi::kApiVersion = "1.16.1";

struct SubsonicApi::Node {
    QString name;
    QList<QPair<QString, QVariant>> attrs;
    std::vector<Node> children;

    explicit Node(QString n = {}) : name(std::move(n)) {}
    // пустые строки и пустые значения не пишутся вовсе: клиенты ждут, что
    // атрибута нет, а не что он пустой
    Node& set(const QString& key, const QVariant& value) {
        if (!value.isValid() || (value.typeId() == QMetaType::QString && value.toString().isEmpty())) return *this;
        attrs.append({key, value});
        return *this;
    }
    Node& add(Node child) {
        children.push_back(std::move(child));
        return children.back();
    }
};

struct SubsonicApi::Call {
    const IncomingRequest& request;
    std::shared_ptr<HttpResponder> out;
    QString action;
    bool json = false;

    QString param(const QString& key) const { return request.query.queryItemValue(key, QUrl::FullyDecoded); }
    QStringList params(const QString& key) const { return request.query.allQueryItemValues(key, QUrl::FullyDecoded); }
    int number(const QString& key, int fallback, int max) const {
        bool ok = false;
        int n = param(key).toInt(&ok);
        return ok && n >= 0 ? qMin(n, max) : fallback;
    }
};

static QString iso(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toUTC().toString(Qt::ISODateWithMs);
}

static int seconds(const QVariantMap& track) {
    return int((track.value("durationMs").toLongLong() + 500) / 1000);
}

static QString md5(const QString& text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

// в JSON эти элементы всегда массивы, даже из одного: так требует Subsonic
static const QSet<QString>& listNames() {
    static const QSet<QString> names{"song",
                                     "album",
                                     "artist",
                                     "playlist",
                                     "entry",
                                     "child",
                                     "index",
                                     "genre",
                                     "musicFolder",
                                     "episode",
                                     "channel",
                                     "internetRadioStation",
                                     "openSubsonicExtensions"};
    return names;
}

template <typename N> static QJsonObject nodeJson(const N& node) {
    QJsonObject object;
    for (const auto& [key, value] : node.attrs) object.insert(key, QJsonValue::fromVariant(value));
    QHash<QString, QJsonArray> lists;
    QStringList order;
    for (const auto& child : node.children) {
        if (!listNames().contains(child.name)) {
            object.insert(child.name, nodeJson(child));
            continue;
        }
        if (!lists.contains(child.name)) order.append(child.name);
        lists[child.name].append(nodeJson(child));
    }
    for (const QString& name : order) object.insert(name, lists.value(name));
    return object;
}

template <typename N> static void writeXml(QXmlStreamWriter& xml, const N& node) {
    xml.writeStartElement(node.name);
    for (const auto& [key, value] : node.attrs) {
        xml.writeAttribute(key,
                           value.typeId() == QMetaType::Bool ? (value.toBool() ? "true" : "false") : value.toString());
    }
    for (const auto& child : node.children) writeXml(xml, child);
    xml.writeEndElement();
}

static QByteArray imageType(const QString& path) {
    QFile file(path);
    QByteArray head = file.open(QIODevice::ReadOnly) ? file.read(12) : QByteArray();
    if (head.startsWith("\x89PNG")) return "image/png";
    if (head.startsWith("RIFF") && head.mid(8, 4) == "WEBP") return "image/webp";
    if (head.startsWith("GIF8")) return "image/gif";
    return "image/jpeg";
}

SubsonicApi::SubsonicApi(SubsonicBackend* backend, const QString& dataDir, QObject* parent)
    : QObject(parent), m_backend(backend), m_seenPath(dataDir + "/phone_seen.json"),
      m_clock([] { return QDateTime::currentMSecsSinceEpoch(); }) {
    BackgroundWriter::waitForAll();
    QFile file(m_seenPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject seen = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = seen.begin(); it != seen.end(); ++it) m_seen.insert(it.key(), qint64(it.value().toDouble()));
    }

    m_handlers = {
        {"ping", &SubsonicApi::ping},
        {"getLicense", &SubsonicApi::license},
        {"getMusicFolders", &SubsonicApi::musicFolders},
        {"getGenres", &SubsonicApi::genres},
        {"getArtists", &SubsonicApi::artists},
        {"getIndexes", &SubsonicApi::indexes},
        {"getArtist", &SubsonicApi::artist},
        {"getAlbum", &SubsonicApi::album},
        {"getSong", &SubsonicApi::song},
        {"getMusicDirectory", &SubsonicApi::musicDirectory},
        {"getAlbumList", &SubsonicApi::albumList},
        {"getAlbumList2", &SubsonicApi::albumList},
        {"getPlaylists", &SubsonicApi::playlists},
        {"getPlaylist", &SubsonicApi::playlist},
        {"createPlaylist", &SubsonicApi::createPlaylist},
        {"updatePlaylist", &SubsonicApi::updatePlaylist},
        {"deletePlaylist", &SubsonicApi::deletePlaylist},
        {"getStarred", &SubsonicApi::starred},
        {"getStarred2", &SubsonicApi::starred},
        {"star", &SubsonicApi::star},
        {"unstar", &SubsonicApi::unstar},
        {"search2", &SubsonicApi::search},
        {"search3", &SubsonicApi::search},
        {"getRandomSongs", &SubsonicApi::randomSongs},
        {"scrobble", &SubsonicApi::scrobble},
        {"stream", &SubsonicApi::stream},
        {"download", &SubsonicApi::stream},
        {"getCoverArt", &SubsonicApi::coverArt},
        {"getUser", &SubsonicApi::user},
        {"getScanStatus", &SubsonicApi::scanStatus},
    };
    // Этого у morph нет, но клиенты спрашивают при синхронизации. Пустой ответ
    // лучше ошибки: на ошибку Amperfy останавливает всю синхронизацию
    for (const char* action : {"getPodcasts",
                               "getNewestPodcasts",
                               "getInternetRadioStations",
                               "getOpenSubsonicExtensions",
                               "getSimilarSongs",
                               "getSimilarSongs2",
                               "getTopSongs",
                               "getArtistInfo",
                               "getArtistInfo2",
                               "getAlbumInfo",
                               "getAlbumInfo2",
                               "getLyrics",
                               "getBookmarks",
                               "getShares",
                               "getNowPlaying",
                               "getPlayQueue",
                               "savePlayQueue",
                               "setRating",
                               "getVideos",
                               "getChatMessages"}) {
        m_handlers.insert(action, &SubsonicApi::empty);
    }
}

void SubsonicApi::setCredentials(const QString& user, const QString& password) {
    m_user = user;
    m_password = password;
}

const SubsonicCatalog& SubsonicApi::catalog() {
    refresh();
    return m_catalog;
}

void SubsonicApi::refresh() {
    quint64 revision = m_backend->revision();
    if (m_built && revision == m_builtRevision) return;
    QList<SubsonicCatalog::PlaylistSource> playlists;
    for (const QString& name : m_backend->playlistNames()) {
        playlists.append({name, m_backend->playlistCover(name), m_backend->playlistTracks(name)});
    }
    int before = int(m_seen.size());
    QList<QPair<QString, QVariantMap>> previous;
    for (const auto* song : m_catalog.songs()) previous.append({song->id, song->track});
    m_catalog.build(m_backend->library(), playlists, m_backend->mixes(), m_seen, m_clock());
    for (const auto& [id, track] : std::as_const(previous)) {
        if (!m_catalog.song(id)) m_gone.insert(id, track);
    }
    m_built = true;
    m_builtRevision = revision;
    if (m_seen.size() != before) saveSeen();
}

void SubsonicApi::saveSeen() const {
    QJsonObject seen;
    for (auto it = m_seen.begin(); it != m_seen.end(); ++it) seen.insert(it.key(), double(it.value()));
    BackgroundWriter::write(m_seenPath, seen);
}

void SubsonicApi::handle(const IncomingRequest& request, std::shared_ptr<HttpResponder> responder) {
    if (!request.path.startsWith("/rest/")) {
        responder->send(404, "text/plain", "morph");
        return;
    }
    QString action = request.path.mid(6);
    if (action.endsWith(".view")) action.chop(5);
    Call call{request, std::move(responder), action, false};
    call.json = call.param("f") == "json";
    if (!authorized(call)) return;
    emit clientSeen(call.param("c"));

    refresh();
    Handler handler = m_handlers.value(action);
    if (!handler) {
        error(call, 0, "Not implemented: " + action);
        return;
    }
    (this->*handler)(call);
}

bool SubsonicApi::authorized(Call& call) {
    QString user = call.param("u");
    QString password = call.param("p");
    QString token = call.param("t");
    QString salt = call.param("s");
    if (user.isEmpty() || (password.isEmpty() && (token.isEmpty() || salt.isEmpty()))) {
        error(call, 10, "Required parameter is missing");
        return false;
    }
    bool ok = !m_password.isEmpty() && user == m_user;
    if (ok && !token.isEmpty()) {
        ok = token.toLower() == md5(m_password + salt);
    } else if (ok) {
        if (password.startsWith("enc:")) password = QString::fromUtf8(QByteArray::fromHex(password.mid(4).toLatin1()));
        ok = password == m_password;
    }
    if (!ok) error(call, 40, "Wrong username or password");
    return ok;
}

void SubsonicApi::reply(Call& call, Node payload) {
    Node root("subsonic-response");
    root.set("status", payload.name == "error" ? "failed" : "ok").set("version", kApiVersion).set("type", "morph");
    if (!payload.name.isEmpty()) root.add(std::move(payload));

    if (call.json) {
        // сам ответ (album, song, playlist) объект, массивами идут только списки внутри
        QJsonObject body;
        for (const auto& [key, value] : root.attrs) body.insert(key, QJsonValue::fromVariant(value));
        for (const auto& child : root.children) body.insert(child.name, nodeJson(child));
        call.out->send(200, "application/json",
                       QJsonDocument(QJsonObject{{root.name, body}}).toJson(QJsonDocument::Compact));
        return;
    }
    QByteArray body;
    QXmlStreamWriter xml(&body);
    xml.writeStartDocument();
    xml.writeDefaultNamespace("http://subsonic.org/restapi");
    writeXml(xml, root);
    xml.writeEndDocument();
    call.out->send(200, "text/xml; charset=UTF-8", body);
}

void SubsonicApi::ok(Call& call) {
    reply(call, Node());
}

void SubsonicApi::error(Call& call, int code, const QString& message) {
    reply(call, std::move(Node("error").set("code", code).set("message", message)));
}

SubsonicApi::Node SubsonicApi::songNode(const SubsonicCatalog::Song& song, const QString& tag) const {
    const QVariantMap& t = song.track;
    const SubsonicCatalog::Album* album = m_catalog.album(song.albumId);
    SubsonicBackend::AudioInfo info = m_backend->audioInfo(t);
    Node node(tag);
    node.set("id", song.id)
        .set("parent", song.albumId)
        .set("isDir", false)
        .set("title", t.value("title").toString())
        .set("album", album ? album->name : QString())
        .set("artist", t.value("artist").toString())
        .set("track", song.number)
        .set("coverArt", song.coverId)
        .set("size", info.size > 0 ? QVariant(info.size) : QVariant())
        .set("contentType", QString::fromLatin1(info.contentType))
        .set("suffix", info.suffix)
        .set("duration", seconds(t))
        .set("albumId", song.albumId)
        .set("artistId", song.artistId)
        .set("type", "music")
        .set("created", iso(song.created));
    if (m_backend->isLiked(t)) node.set("starred", iso(song.created));
    return node;
}

SubsonicApi::Node SubsonicApi::albumNode(const SubsonicCatalog::Album& album) const {
    Node node("album");
    node.set("id", album.id)
        .set("name", album.name)
        .set("artist", album.artist)
        .set("artistId", album.artistId)
        .set("coverArt", album.coverId)
        .set("songCount", int(album.songs.size()))
        .set("duration", album.duration)
        .set("created", iso(album.created));
    return node;
}

SubsonicApi::Node SubsonicApi::artistNode(const SubsonicCatalog::Artist& artist) const {
    Node node("artist");
    node.set("id", artist.id)
        .set("name", artist.name)
        .set("coverArt", artist.coverId)
        .set("albumCount", int(artist.albums.size()));
    return node;
}

SubsonicApi::Node SubsonicApi::playlistNode(const SubsonicCatalog::Playlist& playlist) const {
    Node node("playlist");
    node.set("id", playlist.id)
        .set("name", playlist.name)
        .set("owner", m_user)
        .set("public", false)
        .set("songCount", int(playlist.songs.size()))
        .set("duration", playlist.duration)
        .set("coverArt", playlist.coverId)
        .set("created", iso(0))
        .set("changed", iso(m_clock()));
    return node;
}

QVariantMap SubsonicApi::trackOf(const QString& songId) const {
    if (const auto* s = m_catalog.song(songId)) return s->track;
    return m_gone.value(songId);
}

QList<QVariantMap> SubsonicApi::tracksFor(const QStringList& songIds) const {
    QList<QVariantMap> tracks;
    for (const QString& id : songIds) {
        QVariantMap track = trackOf(id);
        if (!track.isEmpty()) tracks.append(track);
    }
    return tracks;
}

static QVariantList toList(const QList<QVariantMap>& tracks) {
    QVariantList out;
    for (const QVariantMap& t : tracks) out.append(t);
    return out;
}

void SubsonicApi::ping(Call& call) {
    ok(call);
}

void SubsonicApi::license(Call& call) {
    reply(call, std::move(Node("license").set("valid", true)));
}

void SubsonicApi::musicFolders(Call& call) {
    Node folders("musicFolders");
    folders.add(std::move(Node("musicFolder").set("id", 1).set("name", "morph")));
    reply(call, std::move(folders));
}

void SubsonicApi::genres(Call& call) {
    reply(call, Node("genres"));
}

void SubsonicApi::artists(Call& call) {
    Node root(call.action == "getIndexes" ? "indexes" : "artists");
    root.set("ignoredArticles", "");
    if (call.action == "getIndexes") root.set("lastModified", m_clock());
    Node* index = nullptr;
    for (const auto* a : m_catalog.artists()) {
        QString letter = SubsonicCatalog::indexLetter(a->name);
        if (!index || index->attrs.value(0).second.toString() != letter) {
            index = &root.add(std::move(Node("index").set("name", letter)));
        }
        index->add(artistNode(*a));
    }
    reply(call, std::move(root));
}

void SubsonicApi::indexes(Call& call) {
    artists(call);
}

void SubsonicApi::artist(Call& call) {
    const auto* a = m_catalog.artist(call.param("id"));
    if (!a) return error(call, 70, "Artist not found");
    Node node = artistNode(*a);
    for (const QString& id : a->albums) node.add(albumNode(*m_catalog.album(id)));
    reply(call, std::move(node));
}

void SubsonicApi::album(Call& call) {
    const auto* a = m_catalog.album(call.param("id"));
    if (!a) return error(call, 70, "Album not found");
    Node node = albumNode(*a);
    for (const QString& id : a->songs) node.add(songNode(*m_catalog.song(id), "song"));
    reply(call, std::move(node));
}

void SubsonicApi::song(Call& call) {
    const auto* s = m_catalog.song(call.param("id"));
    if (!s) return error(call, 70, "Song not found");
    reply(call, songNode(*s, "song"));
}

// Папки для старых клиентов: корень, в нём артисты, в них альбомы, в них песни
void SubsonicApi::musicDirectory(Call& call) {
    QString id = call.param("id");
    Node dir("directory");
    if (id == "1") {
        dir.set("id", "1").set("name", "morph");
        for (const auto* a : m_catalog.artists()) {
            dir.add(std::move(Node("child")
                                  .set("id", a->id)
                                  .set("parent", "1")
                                  .set("isDir", true)
                                  .set("title", a->name)
                                  .set("coverArt", a->coverId)));
        }
    } else if (const auto* a = m_catalog.artist(id)) {
        dir.set("id", a->id).set("parent", "1").set("name", a->name);
        for (const QString& albumId : a->albums) {
            const auto* al = m_catalog.album(albumId);
            dir.add(std::move(Node("child")
                                  .set("id", al->id)
                                  .set("parent", a->id)
                                  .set("isDir", true)
                                  .set("title", al->name)
                                  .set("artist", al->artist)
                                  .set("coverArt", al->coverId)));
        }
    } else if (const auto* al = m_catalog.album(id)) {
        dir.set("id", al->id).set("parent", al->artistId).set("name", al->name);
        for (const QString& songId : al->songs) dir.add(songNode(*m_catalog.song(songId), "child"));
    } else {
        return error(call, 70, "Directory not found");
    }
    reply(call, std::move(dir));
}

void SubsonicApi::albumList(Call& call) {
    QString type = call.param("type");
    int size = call.number("size", 10, 500);
    int offset = call.number("offset", 0, 1 << 30);
    // счётчиков по альбомам у сервера нет: недавнее и частое это просто новое
    static const QSet<QString> newest{"newest", "recent", "frequent", "highest"};
    QList<const SubsonicCatalog::Album*> list;
    if (newest.contains(type)) {
        list = m_catalog.albums("newest");
    } else if (type == "alphabeticalByArtist") {
        list = m_catalog.albums("alphabeticalByArtist");
    } else if (type == "starred") {
        const auto* main = m_catalog.playlist(SubsonicCatalog::kMainId);
        const QSet<QString> liked(main->songs.begin(), main->songs.end());
        for (const auto* a : m_catalog.albums("alphabeticalByName")) {
            if (std::any_of(a->songs.begin(), a->songs.end(), [&](const QString& id) { return liked.contains(id); })) {
                list.append(a);
            }
        }
    } else if (type == "random") {
        list = m_catalog.albums("alphabeticalByName");
        std::shuffle(list.begin(), list.end(), *QRandomGenerator::global());
    } else if (type == "alphabeticalByName") {
        list = m_catalog.albums("alphabeticalByName");
    }

    bool v2 = call.action == "getAlbumList2";
    Node root(v2 ? "albumList2" : "albumList");
    for (int i = offset; i < list.size() && i < offset + size; ++i) {
        Node node = albumNode(*list[i]);
        if (!v2) node.set("title", list[i]->name).set("isDir", true).set("parent", list[i]->artistId);
        root.add(std::move(node));
    }
    reply(call, std::move(root));
}

void SubsonicApi::playlists(Call& call) {
    Node root("playlists");
    for (const auto* p : m_catalog.playlists()) root.add(playlistNode(*p));
    reply(call, std::move(root));
}

SubsonicApi::Node SubsonicApi::playlistWithSongs(const SubsonicCatalog::Playlist& playlist) const {
    Node node = playlistNode(playlist);
    for (const QString& id : playlist.songs) node.add(songNode(*m_catalog.song(id), "entry"));
    return node;
}

void SubsonicApi::playlist(Call& call) {
    const auto* p = m_catalog.playlist(call.param("id"));
    if (!p) return error(call, 70, "Playlist not found");
    reply(call, playlistWithSongs(*p));
}

void SubsonicApi::createPlaylist(Call& call) {
    QStringList songs = call.params("songId");
    // с playlistId это замена состава существующего плейлиста
    if (!call.param("playlistId").isEmpty()) {
        const auto* p = m_catalog.playlist(call.param("playlistId"));
        if (!p) return error(call, 70, "Playlist not found");
        if (p->main) return error(call, 50, "MAIN follows your likes");
        QString name = p->name;
        m_backend->updatePlaylist(name, name, toList(tracksFor(songs)));
        refresh();
        return reply(call, playlistWithSongs(*m_catalog.playlist(call.param("playlistId"))));
    }
    QString name = call.param("name").trimmed();
    if (name.isEmpty()) return error(call, 10, "Required parameter is missing: name");
    QString saved = m_backend->createPlaylist(name == "MAIN" ? name + " 2" : name, toList(tracksFor(songs)));
    if (saved.isEmpty()) return error(call, 0, "Could not create playlist " + name);
    refresh();
    for (const auto* p : m_catalog.playlists()) {
        if (p->name == saved) return reply(call, playlistWithSongs(*p));
    }
    error(call, 0, "Could not create playlist " + name);
}

void SubsonicApi::updatePlaylist(Call& call) {
    const auto* p = m_catalog.playlist(call.param("playlistId"));
    if (!p) return error(call, 70, "Playlist not found");
    QStringList songs = p->songs;

    // индексы удаления относятся к старому составу: снимаем с конца
    QList<int> remove;
    for (const QString& index : call.params("songIndexToRemove")) {
        bool ok = false;
        int i = index.toInt(&ok);
        if (ok && i >= 0 && i < songs.size() && !remove.contains(i)) remove.append(i);
    }
    std::sort(remove.begin(), remove.end(), std::greater<int>());
    for (int i : remove) songs.removeAt(i);
    for (const QString& id : call.params("songIdToAdd")) {
        if (!trackOf(id).isEmpty()) songs.append(id);
    }

    if (p->main) {
        // MAIN это лайки. Перестановка в Amperfy приходит как «убрать всё и
        // добавить всё заново», поэтому сравниваются составы, а не правки
        const QSet<QString> before(p->songs.begin(), p->songs.end());
        const QSet<QString> after(songs.begin(), songs.end());
        for (const QString& id : after - before) m_backend->setLiked(trackOf(id), true);
        for (const QString& id : before - after) m_backend->setLiked(trackOf(id), false);
        return ok(call);
    }
    QString name = call.param("name").trimmed();
    if (name.isEmpty() || name == "MAIN") name = p->name;
    // занятое имя не повод терять правку состава: плейлист остаётся со старым
    if (!m_backend->updatePlaylist(p->name, name, toList(tracksFor(songs))) && name != p->name) {
        m_backend->updatePlaylist(p->name, p->name, toList(tracksFor(songs)));
    }
    ok(call);
}

void SubsonicApi::deletePlaylist(Call& call) {
    const auto* p = m_catalog.playlist(call.param("id"));
    if (!p) return error(call, 70, "Playlist not found");
    if (p->main) return error(call, 50, "MAIN follows your likes");
    m_backend->deletePlaylist(p->name);
    ok(call);
}

void SubsonicApi::starred(Call& call) {
    Node root(call.action == "getStarred2" ? "starred2" : "starred");
    const auto* main = m_catalog.playlist(SubsonicCatalog::kMainId);
    for (const QString& id : main->songs) root.add(songNode(*m_catalog.song(id), "song"));
    reply(call, std::move(root));
}

void SubsonicApi::star(Call& call) {
    for (const QString& id : call.params("id")) {
        QVariantMap track = trackOf(id);
        if (!track.isEmpty() && !m_backend->isLiked(track)) m_backend->setLiked(track, true);
    }
    ok(call);
}

void SubsonicApi::unstar(Call& call) {
    for (const QString& id : call.params("id")) {
        QVariantMap track = trackOf(id);
        if (!track.isEmpty() && m_backend->isLiked(track)) m_backend->setLiked(track, false);
    }
    ok(call);
}

void SubsonicApi::search(Call& call) {
    QString query = call.param("query").trimmed();
    // Symfonium и другие просят всё пустым запросом или парой кавычек
    if (query == "\"\"") query.clear();
    const QStringList words = query.toLower().split(' ', Qt::SkipEmptyParts);
    auto matches = [&](const QStringList& fields) {
        QString text = fields.join(' ').toLower();
        return std::all_of(words.begin(), words.end(), [&](const QString& w) { return text.contains(w); });
    };
    auto page = [&](auto list, const QString& prefix, auto make, Node& root) {
        int count = call.number(prefix + "Count", 20, 500);
        int offset = call.number(prefix + "Offset", 0, 1 << 30);
        for (int i = offset; i < list.size() && i < offset + count; ++i) root.add(make(list[i]));
    };

    bool v3 = call.action == "search3";
    Node root(v3 ? "searchResult3" : "searchResult2");
    QList<const SubsonicCatalog::Artist*> artists;
    for (const auto* a : m_catalog.artists()) {
        if (matches({a->name})) artists.append(a);
    }
    QList<const SubsonicCatalog::Album*> albums;
    for (const auto* a : m_catalog.albums("alphabeticalByName")) {
        if (matches({a->name, a->artist})) albums.append(a);
    }
    QList<const SubsonicCatalog::Song*> songs;
    for (const auto* s : m_catalog.songs()) {
        if (matches({s->track.value("title").toString(), s->track.value("artist").toString(),
                     s->track.value("album").toString()})) {
            songs.append(s);
        }
    }
    page(artists, "artist", [&](const SubsonicCatalog::Artist* a) { return artistNode(*a); }, root);
    page(albums, "album", [&](const SubsonicCatalog::Album* a) { return albumNode(*a); }, root);
    page(songs, "song", [&](const SubsonicCatalog::Song* s) { return songNode(*s, "song"); }, root);
    reply(call, std::move(root));
}

void SubsonicApi::randomSongs(Call& call) {
    QList<const SubsonicCatalog::Song*> songs = m_catalog.songs();
    std::shuffle(songs.begin(), songs.end(), *QRandomGenerator::global());
    int size = call.number("size", 10, 500);
    Node root("randomSongs");
    for (int i = 0; i < songs.size() && i < size; ++i) root.add(songNode(*songs[i], "song"));
    reply(call, std::move(root));
}

// Трек, который начали и сменили другим раньше, чем Amperfy счёл его
// прослушанным, это пропуск. Пропуск дольше половины трека уже не пропуск,
// а скорее пауза, и про него лучше промолчать
void SubsonicApi::finishPlaying(qint64 now) {
    if (m_playingId.isEmpty()) return;
    QVariantMap track = trackOf(m_playingId);
    double played = double(now - m_playingSince) / 1000.0;
    double length = seconds(track);
    if (!track.isEmpty() && played >= 0 && played < qMax(30.0, length / 2)) {
        m_backend->listened(track, played, length, false);
    }
    m_playingId.clear();
}

void SubsonicApi::scrobble(Call& call) {
    bool submission = call.param("submission") != "false";
    qint64 now = m_clock();
    QString time = call.param("time");
    // скробблы, накопленные без сети, приходят пачкой с временем прослушивания
    bool live = time.isEmpty() || now - time.toLongLong() < 5 * 60 * 1000;
    for (const QString& id : call.params("id")) {
        QVariantMap track = trackOf(id);
        if (track.isEmpty()) continue;
        if (!submission) {
            // то же самое после паузы приходит ещё раз, это не новый трек
            if (!live || id == m_playingId) continue;
            finishPlaying(now);
            m_playingId = id;
            m_playingSince = now;
            continue;
        }
        if (live && id == m_playingId) m_playingId.clear();
        double length = seconds(track);
        m_backend->listened(track, length, length, true);
    }
    ok(call);
}

void SubsonicApi::stream(Call& call) {
    QVariantMap track = trackOf(call.param("id"));
    if (track.isEmpty()) return error(call, 70, "Song not found");
    auto out = call.out;
    QByteArray range = call.request.headers.value("range");
    m_backend->audio(track, [out, range](const QString& path, const QByteArray& contentType) {
        if (path.isEmpty()) out->send(404, "text/plain", "Track is not available right now");
        else out->sendFile(path, contentType, range);
    });
}

void SubsonicApi::coverArt(Call& call) {
    QString id = call.param("id");
    // часть клиентов просит обложку по id альбома или песни, а не по coverArt
    if (const auto* a = m_catalog.album(id)) id = a->coverId;
    else if (const auto* s = m_catalog.song(id)) id = s->coverId;
    else if (const auto* ar = m_catalog.artist(id)) id = ar->coverId;
    QString url = m_catalog.coverUrl(id);
    if (url.isEmpty()) return error(call, 70, "Cover not found");
    auto out = call.out;
    m_backend->cover(url, [out](const QString& path) {
        if (path.isEmpty()) out->send(404, "text/plain", "Cover is not available right now");
        else out->sendFile(path, imageType(path), {});
    });
}

void SubsonicApi::user(Call& call) {
    Node node("user");
    node.set("username", m_user)
        .set("scrobblingEnabled", true)
        .set("adminRole", false)
        .set("settingsRole", false)
        .set("downloadRole", true)
        .set("uploadRole", false)
        .set("playlistRole", true)
        .set("coverArtRole", true)
        .set("commentRole", false)
        .set("podcastRole", false)
        .set("streamRole", true)
        .set("jukeboxRole", false)
        .set("shareRole", false);
    reply(call, std::move(node));
}

void SubsonicApi::scanStatus(Call& call) {
    reply(call, std::move(Node("scanStatus").set("scanning", false).set("count", m_catalog.songCount())));
}

void SubsonicApi::empty(Call& call) {
    static const QSet<QString> silent{"savePlayQueue", "setRating", "getPlayQueue"};
    if (silent.contains(call.action)) return ok(call);
    // getPodcasts отвечает элементом podcasts и так далее
    QString name = call.action.mid(3);
    name[0] = name[0].toLower();
    reply(call, Node(name));
}
