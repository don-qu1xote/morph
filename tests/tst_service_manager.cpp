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
};

QTEST_GUILESS_MAIN(TestServiceManager)
#include "tst_service_manager.moc"
