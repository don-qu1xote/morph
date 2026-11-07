#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <memory>

#include "server/HttpServer.h"
#include "server/SubsonicApi.h"
#include "utils/BackgroundWriter.h"

// MAIN, плейлисты и миксы в памяти, без Яндекса и кэша
class FakeBackend : public SubsonicBackend {
public:
    quint64 rev = 1;
    QVariantList lib;
    QStringList order;
    QHash<QString, QVariantList> playlists;
    QVariantList mixList;
    QSet<QString> liked;
    struct Listen {
        QString id;
        double played;
        double length;
        bool completed;
    };
    QList<Listen> listens;
    QStringList audioRequests;
    QString audioPath;
    QString coverPath;

    quint64 revision() const override { return rev; }
    QVariantList library() const override { return lib; }
    QStringList playlistNames() const override { return order; }
    QVariantList playlistTracks(const QString& name) const override { return playlists.value(name); }
    QString playlistCover(const QString&) const override { return {}; }
    QVariantList mixes() const override { return mixList; }
    bool isLiked(const QVariantMap& track) const override { return liked.contains(track["id"].toString()); }
    void setLiked(const QVariantMap& track, bool on) override {
        QString id = track["id"].toString();
        if (on) {
            liked.insert(id);
            lib.prepend(track);
        } else {
            liked.remove(id);
            for (int i = 0; i < lib.size(); ++i) {
                if (lib[i].toMap()["id"] == id) lib.removeAt(i--);
            }
        }
        ++rev;
    }
    void listened(const QVariantMap& track, double played, double length, bool completed) override {
        listens.append({track["id"].toString(), played, length, completed});
    }
    QString createPlaylist(const QString& name, const QVariantList& tracks) override {
        QString saved = name;
        for (int i = 2; playlists.contains(saved); ++i) saved = name + " " + QString::number(i);
        playlists.insert(saved, tracks);
        order.append(saved);
        ++rev;
        return saved;
    }
    bool updatePlaylist(const QString& name, const QString& newName, const QVariantList& tracks) override {
        if (newName != name && playlists.contains(newName)) return false;
        playlists.remove(name);
        playlists.insert(newName, tracks);
        order[order.indexOf(name)] = newName;
        ++rev;
        return true;
    }
    void deletePlaylist(const QString& name) override {
        playlists.remove(name);
        order.removeAll(name);
        ++rev;
    }
    AudioInfo audioInfo(const QVariantMap& track) const override {
        if (track["service"] == "Yandex") return {"audio/mpeg", "mp3", 0};
        return {"audio/mp4", "m4a", 0};
    }
    void audio(const QVariantMap& track, AudioDone done) override {
        audioRequests.append(track["id"].toString());
        done(audioPath, "audio/mpeg");
    }
    void cover(const QString&, CoverDone done) override { done(coverPath); }
};

// Ответ сервера как дерево: так же его разбирает XMLParser в Amperfy
struct El {
    QString name;
    QHash<QString, QString> attrs;
    QList<El> children;
    QString attr(const QString& key) const { return attrs.value(key); }
    QList<El> all(const QString& tag) const {
        QList<El> out;
        for (const El& c : children) {
            if (c.name == tag) out.append(c);
            out.append(c.all(tag));
        }
        return out;
    }
    El first(const QString& tag) const { return all(tag).value(0); }
};

static El parseXml(const QByteArray& body) {
    QXmlStreamReader xml(body);
    QList<El> stack{El{}};
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            El el;
            el.name = xml.name().toString();
            for (const auto& a : xml.attributes()) el.attrs.insert(a.name().toString(), a.value().toString());
            stack.append(el);
        } else if (xml.isEndElement()) {
            El done = stack.takeLast();
            stack.last().children.append(done);
        }
    }
    return stack.value(0).children.value(0);
}

struct Reply {
    int status = 0;
    QByteArray body;
    QHash<QByteArray, QByteArray> headers;
};

class TestSubsonicServer : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    FakeBackend backend;
    std::unique_ptr<SubsonicApi> api;
    HttpServer server;
    QNetworkAccessManager net;
    qint64 now = 1'700'000'000'000;

    static QVariantMap track(const QString& service, const QString& id, const QString& artist, const QString& title,
                             const QString& album = "Album") {
        return {{"service", service},
                {"id", id},
                {"artist", artist},
                {"title", title},
                {"album", album},
                {"durationMs", 200000},
                {"coverUrl", "https://img/" + id}};
    }

    Reply request(const QString& action, QList<QPair<QString, QString>> params = {}, const QByteArray& range = {},
                  bool head = false, bool auth = true) {
        QUrlQuery query;
        if (auth) {
            QString token = QCryptographicHash::hash("secretabc", QCryptographicHash::Md5).toHex();
            params.append({{"u", "morph"}, {"t", token}, {"s", "abc"}, {"v", "1.16.1"}, {"c", "Amperfy"}});
        }
        for (const auto& p : params) query.addQueryItem(p.first, QUrl::toPercentEncoding(p.second));
        QUrl url(QString("http://127.0.0.1:%1/rest/%2.view").arg(server.port()).arg(action));
        url.setQuery(query);
        QNetworkRequest req(url);
        if (!range.isEmpty()) req.setRawHeader("Range", range);
        QNetworkReply* reply = head ? net.head(req) : net.get(req);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        Reply out;
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.body = reply->readAll();
        for (const auto& h : reply->rawHeaderPairs()) out.headers.insert(h.first.toLower(), h.second);
        reply->deleteLater();
        return out;
    }

    El call(const QString& action, const QList<QPair<QString, QString>>& params = {}) {
        return parseXml(request(action, params).body);
    }

    QStringList ids(const QList<El>& elements) {
        QStringList out;
        for (const El& e : elements) out.append(e.attr("id"));
        return out;
    }

    void startApi() {
        api = std::make_unique<SubsonicApi>(&backend, dir.path());
        api->setCredentials("morph", "secret");
        api->setClock([this] { return now; });
    }

private slots:
    void initTestCase() {
        server.setHandler(
            [this](const IncomingRequest& r, std::shared_ptr<HttpResponder> out) { api->handle(r, out); });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QFile audio(dir.filePath("song.mp3"));
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QByteArray bytes;
        for (int i = 0; i < 300000; ++i) bytes.append(char(i % 251));
        audio.write(bytes);
        audio.close();
        backend.audioPath = audio.fileName();
    }

    void init() {
        BackgroundWriter::waitForAll();
        QFile::remove(dir.filePath("phone_seen.json"));
        backend = FakeBackend();
        backend.audioPath = dir.filePath("song.mp3");
        backend.lib = {track("Yandex", "y1", "Zamay", "Неошансон", "Гром"),
                       track("Yandex", "y2", "Zamay", "Ещё", "Гром"),
                       track("SoundCloud", "s1", "Alblak 52", "Мотылёк", "SoundCloud"),
                       track("YouTube", "v1", "Pyrokinesis", "Легенда", "YouTube Music")};
        backend.liked = {"y1", "y2", "s1", "v1"};
        backend.order = {"Night"};
        backend.playlists["Night"] = {track("Yandex", "y2", "Zamay", "Ещё", "Гром"),
                                      track("Yandex", "y9", "Boulevard Depo", "Friendly Fire", "Rapp2")};
        now = 1'700'000'000'000;
        startApi();
    }

    void cleanup() {
        api.reset();
        BackgroundWriter::waitForAll();
    }

    void authAcceptsTokenPasswordAndHexPassword() {
        El ping = call("ping");
        QCOMPARE(ping.name, QString("subsonic-response"));
        QCOMPARE(ping.attr("status"), QString("ok"));
        QCOMPARE(ping.attr("version"), QString("1.16.1"));

        El plain = parseXml(request("ping", {{"u", "morph"}, {"p", "secret"}}, {}, false, false).body);
        QCOMPARE(plain.attr("status"), QString("ok"));
        El hex = parseXml(request("ping", {{"u", "morph"}, {"p", "enc:736563726574"}}, {}, false, false).body);
        QCOMPARE(hex.attr("status"), QString("ok"));

        El wrong = parseXml(request("ping", {{"u", "morph"}, {"p", "nope"}}, {}, false, false).body);
        QCOMPARE(wrong.attr("status"), QString("failed"));
        QCOMPARE(wrong.first("error").attr("code"), QString("40"));
        El missing = parseXml(request("ping", {}, {}, false, false).body);
        QCOMPARE(missing.first("error").attr("code"), QString("10"));
    }

    // Amperfy считает страницы альбомов по сумме albumCount у артистов и
    // листает их параллельно: суммы и страницы обязаны сходиться
    void amperfyInitialSyncSeesEverything() {
        El artists = call("getArtists");
        int albumCount = 0;
        for (const El& a : artists.all("artist")) albumCount += a.attr("albumCount").toInt();
        QCOMPARE(albumCount, 4);

        QStringList paged;
        for (int offset = 0; offset < 8; offset += 2) {
            El page = call("getAlbumList2",
                           {{"type", "alphabeticalByName"}, {"size", "2"}, {"offset", QString::number(offset)}});
            paged += ids(page.all("album"));
        }
        QCOMPARE(paged.size(), 4);
        QCOMPARE(QSet<QString>(paged.begin(), paged.end()).size(), 4);

        El playlists = call("getPlaylists");
        QList<El> list = playlists.all("playlist");
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].attr("name"), QString("MAIN"));
        QCOMPARE(list[0].attr("songCount"), QString("4"));
        QCOMPARE(list[1].attr("name"), QString("Night"));

        // песня из плейлиста и из MAIN одна и та же
        QList<El> main = call("getPlaylist", {{"id", list[0].attr("id")}}).all("entry");
        QList<El> night = call("getPlaylist", {{"id", list[1].attr("id")}}).all("entry");
        QCOMPARE(night[0].attr("id"), main[1].attr("id"));
        QCOMPARE(main[0].attr("title"), QString("Неошансон"));
        QCOMPARE(main[0].attr("contentType"), QString("audio/mpeg"));
        QCOMPARE(main[0].attr("starred").isEmpty(), false);
        QCOMPARE(night[1].attr("starred").isEmpty(), true);
        QVERIFY(main[0].attr("created").endsWith("Z"));

        // альбом Гром из двух песен, у трека с YouTube альбом-заглушка стал синглом
        El album = call("getAlbum", {{"id", main[0].attr("albumId")}}).first("album");
        QCOMPARE(album.attr("name"), QString("Гром"));
        QCOMPARE(album.all("song").size(), 2);
        El single = call("getAlbum", {{"id", main[3].attr("albumId")}}).first("album");
        QCOMPARE(single.attr("name"), QString("Легенда"));
    }

    // MAIN сменил SoundCloud на Яндекс: у телефона та же песня с тем же id
    void songIdSurvivesOtherVersion() {
        QString before = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[2].attr("id");
        backend.lib[2] = track("Yandex", "y5", "Alblak 52", "Мотылёк", "Мотылёк");
        ++backend.rev;
        QString after = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[2].attr("id");
        QCOMPARE(after, before);
    }

    // свежий микс и новый лайк первыми в newest: по ним Amperfy качает новое
    void newestShowsFreshMixAndNewLikeFirst() {
        call("ping");
        now += 3600 * 1000;
        backend.mixList = {QVariantMap{{"id", "daily1-2026-11-06"},
                                       {"name", "Daily Mix 1, Nov 6"},
                                       {"created", now},
                                       {"tracks", QVariantList{track("Yandex", "y1", "Zamay", "Неошансон", "Гром"),
                                                               track("Yandex", "y7", "Saluki", "Тейк", "Тейк")}}}};
        ++backend.rev;
        QList<El> newest = call("getAlbumList2", {{"type", "newest"}, {"size", "3"}}).all("album");
        QCOMPARE(newest[0].attr("name"), QString("Daily Mix 1, Nov 6"));
        QCOMPARE(newest[0].attr("artist"), QString("morph"));
        QCOMPARE(newest[0].attr("songCount"), QString("2"));

        // песня в миксе своя: из альбома Гром она никуда не уходит
        QList<El> mixSongs = call("getAlbum", {{"id", newest[0].attr("id")}}).all("song");
        QString mainId = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[0].attr("id");
        QVERIFY(mixSongs[0].attr("id") != mainId);
        QCOMPARE(mixSongs[0].attr("albumId"), newest[0].attr("id"));

        now += 3600 * 1000;
        backend.setLiked(track("Yandex", "y8", "Markul", "Серпантин", "Sense of Human"), true);
        newest = call("getAlbumList2", {{"type", "newest"}, {"size", "3"}}).all("album");
        QCOMPARE(newest[0].attr("name"), QString("Sense of Human"));
        QCOMPARE(newest[1].attr("name"), QString("Daily Mix 1, Nov 6"));
    }

    // даты первого появления переживают перезапуск сервера
    void createdDatesPersist() {
        QString before = call("getAlbumList2", {{"type", "newest"}, {"size", "1"}}).first("album").attr("created");
        api.reset();
        BackgroundWriter::waitForAll();
        now += 24 * 3600 * 1000;
        startApi();
        QString after = call("getAlbumList2", {{"type", "newest"}, {"size", "1"}}).first("album").attr("created");
        QCOMPARE(after, before);
    }

    void streamServesWholeFileRangesAndHead() {
        QString id = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[0].attr("id");
        QFile file(backend.audioPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray all = file.readAll();

        Reply whole = request("stream", {{"id", id}});
        QCOMPARE(whole.status, 200);
        QCOMPARE(whole.body.size(), all.size());
        QVERIFY(whole.body == all);
        QCOMPARE(whole.headers.value("content-type"), QByteArray("audio/mpeg"));
        QCOMPARE(backend.audioRequests, QStringList({"y1"}));

        Reply part = request("stream", {{"id", id}}, "bytes=1000-1999");
        QCOMPARE(part.status, 206);
        QVERIFY(part.body == all.mid(1000, 1000));
        QCOMPARE(part.headers.value("content-range"), QByteArray("bytes 1000-1999/300000"));

        Reply tail = request("download", {{"id", id}}, "bytes=-500");
        QVERIFY(tail.body == all.right(500));

        Reply beyond = request("stream", {{"id", id}}, "bytes=400000-");
        QCOMPARE(beyond.status, 416);

        Reply head = request("stream", {{"id", id}}, {}, true);
        QCOMPARE(head.status, 200);
        QCOMPARE(head.headers.value("content-length"), QByteArray("300000"));
        QVERIFY(head.body.isEmpty());

        backend.audioPath.clear();
        QCOMPARE(request("stream", {{"id", id}}).status, 404);
    }

    void starAndUnstarAreLikes() {
        QList<El> night =
            call("getPlaylist", {{"id", call("getPlaylists").all("playlist")[1].attr("id")}}).all("entry");
        call("star", {{"id", night[1].attr("id")}});
        QVERIFY(backend.liked.contains("y9"));
        QCOMPARE(call("getStarred2").all("song").size(), 5);

        call("unstar", {{"id", night[1].attr("id")}});
        QVERIFY(!backend.liked.contains("y9"));
        QCOMPARE(call("getStarred2").all("song").size(), 4);

        // Звезду сняли с песни, которой больше нигде нет: из каталога она
        // ушла, но телефон знает её id и может вернуть её одним касанием
        QString mainOnly = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[3].attr("id");
        call("unstar", {{"id", mainOnly}});
        QVERIFY(!backend.liked.contains("v1"));
        QCOMPARE(call("getSong", {{"id", mainOnly}}).first("error").attr("code"), QString("70"));
        call("star", {{"id", mainOnly}});
        QVERIFY(backend.liked.contains("v1"));
        call("unstar", {{"id", mainOnly}});
        El created = call("createPlaylist", {{"name", "Later"}, {"songId", mainOnly}}).first("playlist");
        QCOMPARE(created.attr("songCount"), QString("1"));
    }

    // перестановка MAIN в Amperfy это «убрать всё, добавить всё»: лайки не трогаем
    void reorderingMainKeepsLikes() {
        QStringList main = ids(call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry"));
        QList<QPair<QString, QString>> params{{"playlistId", SubsonicCatalog::kMainId}, {"name", "MAIN"}};
        for (int i = 0; i < main.size(); ++i) params.append({"songIndexToRemove", QString::number(i)});
        for (int i = main.size() - 1; i >= 0; --i) params.append({"songIdToAdd", main[i]});
        QCOMPARE(call("updatePlaylist", params).attr("status"), QString("ok"));
        QCOMPARE(backend.liked.size(), 4);

        call("updatePlaylist", {{"playlistId", SubsonicCatalog::kMainId}, {"songIndexToRemove", "0"}});
        QVERIFY(!backend.liked.contains("y1"));
    }

    void playlistsCanBeCreatedEditedAndDeleted() {
        QStringList main = ids(call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry"));
        El created = call("createPlaylist", {{"name", "Night"}, {"songId", main[0]}}).first("playlist");
        QCOMPARE(created.attr("name"), QString("Night 2"));
        QVERIFY(!created.attr("id").isEmpty());
        QCOMPARE(created.all("entry").size(), 1);

        QString nightId = call("getPlaylists").all("playlist")[1].attr("id");
        call("updatePlaylist",
             {{"playlistId", nightId}, {"name", "Late Night"}, {"songIndexToRemove", "0"}, {"songIdToAdd", main[2]}});
        QCOMPARE(backend.order.value(0), QString("Late Night"));
        QVariantList tracks = backend.playlists.value("Late Night");
        QCOMPARE(tracks.size(), 2);
        QCOMPARE(tracks[0].toMap()["id"].toString(), QString("y9"));
        QCOMPARE(tracks[1].toMap()["id"].toString(), QString("s1"));

        // переименование в занятое имя не теряет правку состава
        QString lateId = call("getPlaylists").all("playlist")[1].attr("id");
        call("updatePlaylist", {{"playlistId", lateId}, {"name", "Night 2"}, {"songIdToAdd", main[3]}});
        QCOMPARE(backend.playlists.value("Late Night").size(), 3);

        QCOMPARE(call("deletePlaylist", {{"id", SubsonicCatalog::kMainId}}).first("error").attr("code"), QString("50"));
        call("deletePlaylist", {{"id", lateId}});
        QCOMPARE(backend.order, QStringList({"Night 2"}));
    }

    // Пропуск виден по тому, что следующий трек начался раньше, чем Amperfy
    // засчитал прошлый. Пауза и повтор того же трека пропуском не считаются
    void scrobblesTellListensFromSkips() {
        QStringList main = ids(call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry"));
        call("scrobble", {{"id", main[0]}, {"submission", "false"}});
        now += 8000;
        call("scrobble", {{"id", main[0]}, {"submission", "false"}});
        now += 4000;
        call("scrobble", {{"id", main[1]}, {"submission", "false"}});
        QCOMPARE(backend.listens.size(), 1);
        QCOMPARE(backend.listens[0].id, QString("y1"));
        QCOMPARE(backend.listens[0].completed, false);
        QCOMPARE(backend.listens[0].played, 12.0);

        now += 150000;
        call("scrobble", {{"id", main[1]}, {"submission", "true"}});
        now += 1000;
        call("scrobble", {{"id", main[2]}, {"submission", "false"}});
        QCOMPARE(backend.listens.size(), 2);
        QCOMPARE(backend.listens[1].id, QString("y2"));
        QCOMPARE(backend.listens[1].completed, true);

        // прослушанное без сети приходит позже со своим временем
        call("scrobble", {{"id", main[3]}, {"submission", "true"}, {"time", QString::number(now - 3600 * 1000)}});
        QCOMPARE(backend.listens.size(), 3);
        QCOMPARE(backend.listens[2].completed, true);
    }

    void searchFindsByWordsInAnyField() {
        El result = call("search3", {{"query", "zamay гром"}});
        QCOMPARE(result.all("song").size(), 2);
        QCOMPARE(result.all("album").size(), 1);
        QCOMPARE(call("search3", {{"query", "\"\""}, {"songCount", "500"}}).all("song").size(), 5);
    }

    // другие клиенты просят JSON: списки там всегда массивы
    void jsonFormatUsesArrays() {
        QString albumId = call("getPlaylist", {{"id", SubsonicCatalog::kMainId}}).all("entry")[3].attr("albumId");
        QJsonObject root = QJsonDocument::fromJson(request("getAlbum", {{"id", albumId}, {"f", "json"}}).body).object();
        QJsonObject response = root["subsonic-response"].toObject();
        QCOMPARE(response["status"].toString(), QString("ok"));
        QJsonArray songs = response["album"].toObject()["song"].toArray();
        QCOMPARE(songs.size(), 1);
        QCOMPARE(songs[0].toObject()["duration"].toInt(), 200);
        QCOMPARE(songs[0].toObject()["isDir"].toBool(true), false);
    }

    // так Amperfy проверяет адрес перед входом: GET корня без пароля, всё от 400 это отказ
    void rootAnswersBeforeLogin() {
        auto fetch = [this](const QString& path) {
            QNetworkReply* reply =
                net.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1%2").arg(server.port()).arg(path))));
            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            return status;
        };
        QCOMPARE(fetch(""), 200);
        QCOMPARE(fetch("/"), 200);
        QCOMPARE(fetch("/server/xml.server.php?action=handshake"), 404);
    }

    void unknownCallsAnswerWithoutBreakingSync() {
        QCOMPARE(call("getPodcasts").first("podcasts").name, QString("podcasts"));
        QCOMPARE(call("getInternetRadioStations").attr("status"), QString("ok"));
        QCOMPARE(call("getSomethingNew").first("error").attr("code"), QString("0"));
        QCOMPARE(request("ping", {}, {}, false, true).status, 200);
    }
};

QTEST_GUILESS_MAIN(TestSubsonicServer)
#include "tst_subsonic_server.moc"
