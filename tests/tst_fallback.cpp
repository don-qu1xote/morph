#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/ServiceManager.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"

// Цепочка целиком: Яндекс не отдаёт трек, его находит yt-dlp, и поток
// приходит под исходным яндексовским id
class TestFallback : public QObject {
    Q_OBJECT

    FakeHttpServer yandexApi;
    FakeHttpServer soundcloudApi;
    FakeTools tools;
    QTemporaryDir dir;

    QString ytDlpCalls() const {
        QFile log(dir.filePath("yt-dlp.log"));
        log.open(QIODevice::ReadOnly);
        return QString::fromUtf8(log.readAll());
    }

    // поиск на SoundCloud находит трек 42 или ничего
    void serveSoundCloud(bool hasTrack) {
        QString base = soundcloudApi.url();
        soundcloudApi.on("GET", "/search/tracks", [hasTrack](const HttpRequest&) {
            QJsonArray found;
            if (hasTrack) found.append(QJsonObject{{"id", 42}});
            return jsonBody({{"collection", found}});
        });
        soundcloudApi.on("GET", "/tracks/42", [base](const HttpRequest&) {
            QJsonObject transcoding{{"url", base + "/media/42"}, {"format", QJsonObject{{"protocol", "progressive"}}}};
            return jsonBody({{"media", QJsonObject{{"transcodings", QJsonArray{transcoding}}}}});
        });
        soundcloudApi.on("GET", "/media/42",
                         [](const HttpRequest&) { return jsonBody({{"url", "https://sc.example/42.mp3"}}); });
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("MORPH_YANDEX_API", yandexApi.url().toUtf8());
        qputenv("MORPH_SOUNDCLOUD_API", soundcloudApi.url().toUtf8());
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        // на запросы с NOPE yt-dlp ничего не находит
        tools.add("yt-dlp", "echo \"$@\" >> \"$YTDLP_LOG\"\n"
                            "case \"$*\" in *NOPE*) exit 1 ;; esac\n"
                            "echo '{\"url\": \"https://cdn.example/found.m4a\", \"abr\": 129.5}'\n");
    }

    void init() { QFile::remove(dir.filePath("yt-dlp.log")); }

    void removedYandexTrackPlaysFromYouTube() {
        yandexApi.on("GET", "/tracks/101/download-info", [](const HttpRequest&) { return HttpResponse{404, "{}"}; });
        yandexApi.on("GET", "/tracks/101", [](const HttpRequest&) {
            QJsonObject track{
                {"id", 101}, {"title", "Молотилка"}, {"artists", QJsonArray{QJsonObject{{"name", "Ежемесячные"}}}}};
            return jsonResult(QJsonArray{track});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy bitrates(&services, &ServiceManager::bitrateReady);

        services.resolve("Yandex", "101");
        QVERIFY(streams.wait());

        QCOMPARE(streams[0][0].toString(), QString("101"));
        QCOMPARE(streams[0][1].toString(), QString("https://cdn.example/found.m4a"));
        QCOMPARE(bitrates.last()[1].toInt(), 130);
        QVERIFY(ytDlpCalls().contains("ytsearch1:Ежемесячные - Молотилка"));
        QVERIFY(ytDlpCalls().contains("bestaudio[ext=m4a]/bestaudio"));
    }

    void textListBecomesPlayableYouTubePlaylist() {
        QFile list(dir.filePath("missing.txt"));
        list.open(QIODevice::WriteOnly);
        list.write("Zamay - Неошансон\n\nZamay - Неошансон\nTitle Only\nВалентин Дядька - Лондон против всех - Live\n");
        list.close();

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy imported(&services, &ServiceManager::playlistImported);
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);

        services.importPlaylist("  " + list.fileName() + "\n");
        QCOMPARE(imported.count(), 1);
        QCOMPARE(imported[0][0].toString(), QString("missing"));

        QVariantList tracks = imported[0][2].toList();
        QCOMPARE(tracks.size(), 3);
        QVariantMap first = tracks[0].toMap();
        QCOMPARE(first["artist"].toString(), QString("Zamay"));
        QCOMPARE(first["title"].toString(), QString("Неошансон"));
        QCOMPARE(first["service"].toString(), QString("YouTube"));
        QCOMPARE(tracks[1].toMap()["title"].toString(), QString("Title Only"));
        QCOMPARE(tracks[2].toMap()["title"].toString(), QString("Лондон против всех - Live"));

        // id годится и как имя файла кэша
        QString id = first["id"].toString();
        QVERIFY(!id.contains('/') && !id.contains('='));

        services.resolve("YouTube", id);
        QVERIFY(streams.wait());
        QCOMPARE(streams[0][0].toString(), id);
        QVERIFY(ytDlpCalls().contains("ytsearch1:Zamay - Неошансон"));
    }

    void queryIdRoundTrip() {
        QString query = "Слава КПСС - Эллен Шейдлин / remix?";
        QString id = YouTubeService::queryTrackId(query);
        QCOMPARE(YouTubeService::queryFromTrackId(id), query);
        QVERIFY(YouTubeService::queryFromTrackId("dQw4w9WgXcQ").isEmpty());
    }

    void emptyFileIsAnError() {
        QFile list(dir.filePath("empty.txt"));
        list.open(QIODevice::WriteOnly);
        list.write("\n\n");
        list.close();

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy errors(&services, &ServiceManager::errorOccurred);
        QSignalSpy imported(&services, &ServiceManager::playlistImported);

        services.importPlaylist(list.fileName());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(imported.count(), 0);
    }
    void notOnYouTubeThenSoundCloud() {
        serveSoundCloud(true);
        yandexApi.on("GET", "/tracks/303/download-info", [](const HttpRequest&) { return HttpResponse{404, "{}"}; });
        yandexApi.on("GET", "/tracks/303", [](const HttpRequest&) {
            QJsonObject track{{"id", 303}, {"title", "NOPE"}, {"artists", QJsonArray{QJsonObject{{"name", "Rare"}}}}};
            return jsonResult(QJsonArray{track});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);

        services.resolve("Yandex", "303");
        QVERIFY(streams.wait());
        QCOMPARE(streams[0][0].toString(), QString("303"));
        QCOMPARE(streams[0][1].toString(), QString("https://sc.example/42.mp3"));
        QCOMPARE(soundcloudApi.requestsTo("/search/tracks").last().query.queryItemValue("q"), QString("Rare - NOPE"));
    }

    void nowhereToBeFoundIsReported() {
        serveSoundCloud(false);
        yandexApi.on("GET", "/tracks/404/download-info", [](const HttpRequest&) { return HttpResponse{404, "{}"}; });
        yandexApi.on("GET", "/tracks/404", [](const HttpRequest&) {
            QJsonObject track{{"id", 404}, {"title", "NOPE"}, {"artists", QJsonArray{QJsonObject{{"name", "Ghost"}}}}};
            return jsonResult(QJsonArray{track});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy errors(&services, &ServiceManager::errorOccurred);

        services.resolve("Yandex", "404");
        auto reported = [&errors]() {
            for (const auto& e : errors) {
                if (e[0].toString().contains("Ghost - NOPE")) return true;
            }
            return false;
        };
        QTRY_VERIFY(reported());
    }

    void importedLineAlsoFallsBackToSoundCloud() {
        serveSoundCloud(true);

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setSoundCloudClientId("client");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);

        QString id = YouTubeService::queryTrackId("Someone - NOPE");
        services.resolve("YouTube", id);
        QVERIFY(streams.wait());
        QCOMPARE(streams[0][0].toString(), id);
        QCOMPARE(streams[0][1].toString(), QString("https://sc.example/42.mp3"));
    }

    void missingYtDlpIsAFailureNotASilentHang() {
        QByteArray path = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        YouTubeService youtube;
        QSignalSpy failed(&youtube, &YouTubeService::resolveFailed);

        youtube.resolveByQuery("Anyone - Anything", "1");
        QVERIFY(failed.count() == 1 || failed.wait());
        qputenv("PATH", path);
        QCOMPARE(failed[0][0].toString(), QString("1"));
    }
};

QTEST_GUILESS_MAIN(TestFallback)
#include "tst_fallback.moc"
