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
    FakeTools tools;
    QTemporaryDir dir;

    QString ytDlpCalls() const {
        QFile log(dir.filePath("yt-dlp.log"));
        log.open(QIODevice::ReadOnly);
        return QString::fromUtf8(log.readAll());
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("MORPH_YANDEX_API", yandexApi.url().toUtf8());
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        tools.add("yt-dlp", "echo \"$@\" >> \"$YTDLP_LOG\"\n"
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
};

QTEST_GUILESS_MAIN(TestFallback)
#include "tst_fallback.moc"
