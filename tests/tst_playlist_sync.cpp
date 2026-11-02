#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/Library.h"
#include "core/PlaylistSync.h"
#include "services/YandexService.h"
#include "support/FakeHttpServer.h"

// Плейлисты morph в Яндексе: заводятся, держат те же треки, переименовываются
// и удаляются вместе с плейлистом в morph
class TestPlaylistSync : public QObject {
    Q_OBJECT

    FakeHttpServer api;
    QTemporaryDir dir;
    // что сейчас лежит в плейлисте 1001 на «сервере»
    QJsonArray serverTracks;
    QString serverTitle;
    int revision = 1;

    static QJsonObject yandexTrack(int id, const QString& artist, const QString& title, int albumId) {
        return {{"id", id},
                {"title", title},
                {"durationMs", 200000},
                {"artists", QJsonArray{QJsonObject{{"name", artist}}}},
                {"albums", QJsonArray{QJsonObject{{"id", albumId}, {"title", "Album"}}}}};
    }

    void serve() {
        api.on("GET", "/account/status",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });
        api.on("POST", "/users/5/playlists/create", [this](const HttpRequest& r) {
            serverTitle = r.form().queryItemValue("title");
            return jsonResult(QJsonObject{{"kind", 1001}, {"revision", revision}});
        });
        api.on("GET", "/users/5/playlists/1001", [this](const HttpRequest&) {
            return jsonResult(
                QJsonObject{{"kind", 1001}, {"revision", revision}, {"title", serverTitle}, {"tracks", serverTracks}});
        });
        api.on("POST", "/users/5/playlists/1001/change", [this](const HttpRequest& r) {
            QJsonArray diff =
                QJsonDocument::fromJson(r.form().queryItemValue("diff", QUrl::FullyDecoded).toUtf8()).array();
            serverTracks = {};
            for (const auto& op : diff) {
                if (op.toObject()["op"] == "insert") {
                    for (const auto& t : op.toObject()["tracks"].toArray()) serverTracks.append(t);
                }
            }
            ++revision;
            return jsonResult(QJsonObject{{"revision", revision}});
        });
        api.on("POST", "/users/5/playlists/1001/name", [this](const HttpRequest& r) {
            serverTitle = r.form().queryItemValue("value");
            return jsonResult(QJsonObject{});
        });
        api.on("POST", "/users/5/playlists/1001/delete", [](const HttpRequest&) { return jsonResult("ok"); });
        // альбом трека 31 известен только из /tracks
        api.on("POST", "/tracks",
               [](const HttpRequest&) { return jsonResult(QJsonArray{yandexTrack(31, "Zamay", "Неошансон", 9)}); });
        // трек с SoundCloud находится в Яндексе как 41
        api.on("GET", "/search", [](const HttpRequest&) {
            return jsonResult(
                QJsonObject{{"tracks", QJsonObject{{"results", QJsonArray{yandexTrack(41, "Rare", "Song", 4)}}}}});
        });
    }

    static QVariantMap playlists(const QString& name) {
        QVariantList tracks{
            QVariantMap{{"id", "31"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Неошансон"}},
            QVariantMap{{"id", "sc7"},
                        {"service", "SoundCloud"},
                        {"artist", "uploader"},
                        {"title", "Rare - Song"},
                        {"durationMs", 200000}},
            QVariantMap{{"id", "31"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Неошансон"}}};
        return {{name, QVariantMap{{"coverUrl", ""}, {"tracks", tracks}}}};
    }

private slots:
    void initTestCase() { qputenv("MORPH_YANDEX_API", api.url().toUtf8()); }

    void playlistLivesInYandexToo() {
        serve();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("t");
        Library library(dir.filePath("library.json"));
        PlaylistSync sync(&yandex, &library, dir.filePath("yandex_playlists.json"));
        QSignalSpy synced(&sync, &PlaylistSync::synced);

        // выключена: ничего не уходит
        sync.sync(playlists("Road"));
        QTest::qWait(200);
        QVERIFY(api.requestsTo("/users/5/playlists/create").isEmpty());

        sync.setEnabled(true);
        QVERIFY(synced.wait());
        QCOMPARE(synced[0][0].toString(), QString("Road"));
        QCOMPARE(synced[0][1].toInt(), 2);
        QCOMPARE(synced[0][2].toInt(), 3);
        QCOMPARE(api.requestsTo("/users/5/playlists/create")[0].form().queryItemValue("visibility"),
                 QString("private"));
        QCOMPARE(serverTitle, QString("Road"));
        QCOMPARE(serverTracks.size(), 2);
        QCOMPARE(serverTracks[0].toObject()["id"].toString(), QString("31"));
        QCOMPARE(serverTracks[0].toObject()["albumId"].toString(), QString("9"));
        QCOMPARE(serverTracks[1].toObject()["id"].toString(), QString("41"));

        // ничего не поменялось: и менять нечего
        int changes = api.requestsTo("/users/5/playlists/1001/change").size();
        sync.sync(playlists("Road"));
        QVERIFY(synced.wait());
        QCOMPARE(api.requestsTo("/users/5/playlists/1001/change").size(), changes);

        // переименовали в morph: тот же плейлист в Яндексе с новым именем
        sync.renamed("Road", "Trip");
        sync.sync(playlists("Trip"));
        QVERIFY(synced.wait());
        QCOMPARE(serverTitle, QString("Trip"));
        QCOMPARE(api.requestsTo("/users/5/playlists/create").size(), 1);

        // удалили в morph: уходит и из Яндекса
        sync.sync({});
        QVERIFY(api.waitFor("/users/5/playlists/1001/delete"));
    }
};

QTEST_GUILESS_MAIN(TestPlaylistSync)
#include "tst_playlist_sync.moc"
