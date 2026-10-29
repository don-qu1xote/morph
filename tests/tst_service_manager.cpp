#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "core/ServiceManager.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"

class TestServiceManager : public QObject {
    Q_OBJECT

    FakeHttpServer yandexApi;
    FakeHttpServer soundcloudApi;
    FakeTools tools;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("MORPH_YANDEX_API", yandexApi.url().toUtf8());
        qputenv("MORPH_SOUNDCLOUD_API", soundcloudApi.url().toUtf8());
        // настоящий yt-dlp тесту не нужен: пустой ответ, как от поиска без результатов
        tools.add("yt-dlp", "exit 0\n");
    }

    void unsupportedImportReportsError() {
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy errors(&services, &ServiceManager::errorOccurred);

        services.importPlaylist("https://example.com/playlist");
        QCOMPARE(errors.count(), 1);
    }

    void searchGoesOnlyToRequestedService() {
        yandexApi.on("GET", "/search", [](const HttpRequest&) {
            QJsonObject track{{"id", 7}, {"title", "Song"}, {"artists", QJsonArray{QJsonObject{{"name", "Artist"}}}}};
            return jsonResult(QJsonObject{{"tracks", QJsonObject{{"results", QJsonArray{track}}}}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy results(&services, &ServiceManager::searchResultsReady);

        services.search("song", "yandex");
        QVERIFY(results.wait());

        QCOMPARE(results[0][0].toString(), QString("Yandex"));
        QVariantMap first = results[0][1].toList().value(0).toMap();
        QCOMPARE(first["title"].toString(), QString("Song"));
        QCOMPARE(first["artist"].toString(), QString("Artist"));
        QVERIFY(first.contains("durationMs"));

        QCOMPARE(yandexApi.requestsTo("/search").last().query.queryItemValue("text"), QString("song"));
        QCOMPARE(yandexApi.requestsTo("/search").last().headers["authorization"], QByteArray("OAuth token"));
        QVERIFY(soundcloudApi.requestsTo("/search/tracks").isEmpty());
    }

    void resolveRoutesByTrackService() {
        QString base = soundcloudApi.url();
        soundcloudApi.on("GET", "/tracks/42", [base](const HttpRequest&) {
            QJsonObject transcoding{{"url", base + "/media/42"}, {"format", QJsonObject{{"protocol", "progressive"}}}};
            return jsonBody({{"media", QJsonObject{{"transcodings", QJsonArray{transcoding}}}}});
        });
        soundcloudApi.on("GET", "/media/42",
                         [](const HttpRequest&) { return jsonBody({{"url", "https://cdn.example/42.mp3"}}); });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setSoundCloudClientId("client");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy bitrates(&services, &ServiceManager::bitrateReady);

        services.resolve("SoundCloud", "42");
        QVERIFY(streams.wait());

        QCOMPARE(streams[0][0].toString(), QString("42"));
        QCOMPARE(streams[0][1].toString(), QString("https://cdn.example/42.mp3"));
        QCOMPARE(bitrates.count(), 1);
        QVERIFY(yandexApi.requestsTo("/tracks/42/download-info").isEmpty());
    }

    void unknownServiceIsIgnored() {
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);

        services.resolve("Nope", "1");
        services.reportPlay("Nope", "1", "");
        QTest::qWait(100);
        QCOMPARE(streams.count(), 0);
    }
    void likesAreFetchedOncePerToken() {
        yandexApi.on("GET", "/account/status",
                     [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });
        yandexApi.on("GET", "/users/5/likes/tracks", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"library", QJsonObject{{"tracks", QJsonArray{}}}}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy fetched(&services, &ServiceManager::likesFetched);

        services.setYandexToken("a");
        QVERIFY(fetched.wait());
        services.setYandexToken("a");
        QTest::qWait(150);
        QCOMPARE(fetched.count(), 1);
        QCOMPARE(fetched[0][0].toString(), QString("Yandex"));
    }

    void likeIsSentOnlyForYandexTracks() {
        yandexApi.on("GET", "/account/status",
                     [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("b");
        yandexApi.requests.clear();

        services.setLiked({{"id", "yt1"}, {"service", "YouTube"}}, true);
        services.setLiked({{"id", "9"}, {"service", "Yandex"}}, true);
        QVERIFY(yandexApi.waitFor("/users/5/likes/tracks/add-multiple"));
        QCOMPARE(yandexApi.requestsTo("/users/5/likes/tracks/add-multiple").size(), 1);
        QCOMPARE(yandexApi.requestsTo("/users/5/likes/tracks/add-multiple")[0].form().queryItemValue("track-ids"),
                 QString("9"));
    }
    void dislikeIsYandexOnly() {
        yandexApi.on("GET", "/account/status",
                     [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("c");
        yandexApi.requests.clear();

        services.dislike({{"id", "sc1"}, {"service", "SoundCloud"}});
        services.dislike({{"id", "8"}, {"service", "Yandex"}});
        QVERIFY(yandexApi.waitFor("/users/5/dislikes/tracks/add-multiple"));
        QCOMPARE(yandexApi.requestsTo("/users/5/dislikes/tracks/add-multiple").size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestServiceManager)
#include "tst_service_manager.moc"
