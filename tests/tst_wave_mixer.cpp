#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/WaveMixer.h"
#include "services/SoundCloudService.h"
#include "services/YouTubeService.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"
#include "utils/PathProvider.h"

class TestWaveMixer : public QObject {
    Q_OBJECT

    FakeHttpServer soundcloudApi;
    FakeTools tools;
    QTemporaryDir dir;

    static QVariantMap track(const QString& service, const QString& id, const QString& artist, const QString& title,
                             qint64 durationMs = 200000) {
        return {{"service", service}, {"id", id}, {"artist", artist}, {"title", title}, {"durationMs", durationMs}};
    }

    // y1..yN, у каждого свой артист: "Artist 3 - Title 3"
    static QVariantList yandexBatch(int from, int to) {
        QVariantList batch;
        for (int i = from; i <= to; ++i) {
            QString n = QString::number(i);
            batch.append(track("Yandex", "y" + n, "Artist " + n, "Title " + n));
        }
        return batch;
    }

    static QStringList ids(const QVariant& tracks) {
        QStringList out;
        for (const QVariant& t : tracks.toList()) out.append(t.toMap()["id"].toString());
        return out;
    }

    static QJsonObject soundcloudTrack(const QString& id, const QString& title, qint64 durationMs = 200000,
                                       const QString& policy = "ALLOW") {
        return {{"id", id},
                {"title", title},
                {"duration", durationMs},
                {"policy", policy},
                {"user", QJsonObject{{"username", "uploader"}}}};
    }

    // поиск всегда находит трек 7, а related у него такие, какие дал тест
    void serveSoundCloud(const QJsonArray& related) {
        soundcloudApi.on("GET", "/search/tracks", [](const HttpRequest&) {
            return jsonBody({{"collection", QJsonArray{QJsonObject{{"id", 7}}}}});
        });
        soundcloudApi.on("GET", "/tracks/7/related",
                         [related](const HttpRequest&) { return jsonBody({{"collection", related}}); });
    }

    // запрос "q" каждого поиска на SoundCloud, по порядку
    QStringList searches() const {
        QStringList queries;
        for (const HttpRequest& r : soundcloudApi.requestsTo("/search/tracks"))
            queries.append(r.query.queryItemValue("q"));
        return queries;
    }

    QString ytDlpCalls() const {
        QFile log(dir.filePath("yt-dlp.log"));
        if (!log.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(log.readAll());
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // yt-dlp запускается в папке кэша, без неё процесс не стартует
        QDir().mkpath(PathProvider::getTrackCachePath());
        qputenv("MORPH_SOUNDCLOUD_API", soundcloudApi.url().toUtf8());
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        // поиск отдаёт клип seedvid, микс по нему два трека; с YTDLP_FAIL падает
        tools.add("yt-dlp",
                  "echo \"$@\" >> \"$YTDLP_LOG\"\n"
                  "[ -n \"$YTDLP_FAIL\" ] && exit 1\n"
                  "case \"$*\" in\n"
                  "  *list=RD*)\n"
                  "    echo '{\"id\": \"yt1\", \"title\": \"Mix One\", \"channel\": \"One - Topic\", "
                  "\"duration\": 180}'\n"
                  "    echo '{\"id\": \"yt2\", \"title\": \"Mix Two\", \"channel\": \"Two\", \"duration\": 200}'\n"
                  "    ;;\n"
                  "  *ytsearch1:*) echo '{\"id\": \"seedvid\", \"title\": \"Seed\"}' ;;\n"
                  "esac\n");
    }

    void init() {
        soundcloudApi.reset();
        QFile::remove(dir.filePath("yt-dlp.log"));
        qunsetenv("YTDLP_FAIL");
    }

    void firstBatchGoesOutAtOnceAndStartsTheSearch() {
        serveSoundCloud({soundcloudTrack("sc1", "Related One")});
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken("client-id");
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);
        QSignalSpy related(&soundcloud, &SoundCloudService::relatedReady);
        QSignalSpy radio(&youtube, &YouTubeService::radioReady);

        mixer.addBatch("Yandex", yandexBatch(1, 5));
        QCOMPARE(wave.size(), 1);
        QCOMPARE(wave[0][0].toString(), QString("Yandex"));
        QCOMPARE(ids(wave[0][1]), QStringList({"y1", "y2", "y3", "y4", "y5"}));

        QVERIFY(related.wait());
        QVERIFY(!radio.isEmpty() || radio.wait());
        QCOMPARE(searches().first(), QString("Artist 1 - Title 1"));
        QCOMPARE(soundcloudApi.requestsTo("/tracks/7/related")[0].query.queryItemValue("limit"), QString("10"));
        QVERIFY(ytDlpCalls().contains("ytsearch1:Artist 2 - Title 2"));
        QVERIFY(ytDlpCalls().contains("--playlist-items 2-11 https://www.youtube.com/watch?v=seedvid&list=RDseedvid"));
    }

    void nextBatchInterleavesSoundCloudAndYouTube() {
        serveSoundCloud({soundcloudTrack("sc1", "Related One"), soundcloudTrack("sc2", "Related Two")});
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken("client-id");
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);
        QSignalSpy related(&soundcloud, &SoundCloudService::relatedReady);
        QSignalSpy radio(&youtube, &YouTubeService::radioReady);

        mixer.addBatch("Yandex", yandexBatch(1, 5));
        QVERIFY(related.wait());
        QVERIFY(!radio.isEmpty() || radio.wait());

        mixer.addBatch("Yandex", yandexBatch(6, 10));
        QCOMPARE(ids(wave[1][1]), QStringList({"y6", "y7", "sc1", "y8", "y9", "yt1", "y10"}));

        QVariantList mixed = wave[1][1].toList();
        QCOMPARE(mixed[2].toMap()["service"].toString(), QString("SoundCloud"));
        QCOMPARE(mixed[5].toMap()["service"].toString(), QString("YouTube"));
        QCOMPARE(mixed[5].toMap()["artist"].toString(), QString("One"));

        // пулы не пустые, но ниже порога: пошёл новый поиск
        QVERIFY(related.size() > 1 || related.wait());
        QVERIFY(radio.size() > 1 || radio.wait());
        QVERIFY(searches().contains("Artist 6 - Title 6"));
    }

    void finishedTrackBecomesTheNextSeed() {
        serveSoundCloud({soundcloudTrack("sc1", "Related One")});
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken("client-id");
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy related(&soundcloud, &SoundCloudService::relatedReady);
        QSignalSpy radio(&youtube, &YouTubeService::radioReady);

        mixer.addBatch("Yandex", yandexBatch(1, 5));
        QVERIFY(related.wait());
        QVERIFY(!radio.isEmpty() || radio.wait());

        mixer.trackFinished("y4");
        mixer.addBatch("Yandex", yandexBatch(6, 10));
        QVERIFY(related.size() > 1 || related.wait());
        QVERIFY(searches().contains("Artist 4 - Title 4"));
        QVERIFY(radio.size() > 1 || radio.wait());
    }

    void repeatsSnippetsAndSetsStayOut() {
        qputenv("YTDLP_FAIL", "1");
        serveSoundCloud({
            soundcloudTrack("dup", "Artist 1 - Title 1 (Official Audio)"),
            soundcloudTrack("short", "Jingle", 30000),
            soundcloudTrack("long", "Two Hour Set", 2 * 60 * 60 * 1000),
            soundcloudTrack("preview", "Go+ Only", 200000, "SNIP"),
            soundcloudTrack("ok", "Fine Track"),
        });
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken("client-id");
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);
        QSignalSpy related(&soundcloud, &SoundCloudService::relatedReady);
        QSignalSpy radio(&youtube, &YouTubeService::radioReady);

        mixer.addBatch("Yandex", yandexBatch(1, 5));
        QVERIFY(related.wait());
        QVERIFY(!radio.isEmpty() || radio.wait());
        QCOMPARE(radio[0][0].toList().size(), 0);

        mixer.addBatch("Yandex", yandexBatch(6, 10));
        QCOMPARE(ids(wave[1][1]), QStringList({"y6", "y7", "ok", "y8", "y9", "y10"}));
        QVERIFY(related.size() > 1 || related.wait());
        QVERIFY(radio.size() > 1 || radio.wait());
    }

    void removedLikesTakeEveryFifthSlot() {
        qputenv("YTDLP_FAIL", "1");
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);
        QSignalSpy radio(&youtube, &YouTubeService::radioReady);

        mixer.setLostTracks({track("Yandex", "lost1", "Gone", "Away")});
        mixer.addBatch("Yandex", yandexBatch(1, 10));

        QStringList expected;
        for (int i = 1; i <= 10; ++i) expected.append("y" + QString::number(i));
        expected.append("lost1");
        QCOMPARE(ids(wave[0][1]), expected);

        // без токена SoundCloud не спрашиваем вовсе, сидом для YouTube стал убранный лайк
        QVERIFY(radio.wait());
        QCOMPARE(soundcloudApi.requests.size(), 0);
        QVERIFY(ytDlpCalls().contains("ytsearch1:Gone - Away"));
    }

    void disabledMixerLeavesTheWaveAlone() {
        serveSoundCloud({soundcloudTrack("sc1", "Related One")});
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken("client-id");
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);

        mixer.setEnabled(false);
        mixer.addBatch("Yandex", yandexBatch(1, 5));
        QCOMPARE(ids(wave[0][1]), QStringList({"y1", "y2", "y3", "y4", "y5"}));

        QTest::qWait(150);
        QCOMPARE(soundcloudApi.requests.size(), 0);
        QVERIFY(ytDlpCalls().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWaveMixer)
#include "tst_wave_mixer.moc"
