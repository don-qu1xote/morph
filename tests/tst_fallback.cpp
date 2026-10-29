#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/ServiceManager.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"
#include "utils/PathProvider.h"

// Цепочка целиком: Яндекс не отдаёт трек, замена находится на SoundCloud или
// YouTube, и поток приходит под исходным яндексовским id
class TestFallback : public QObject {
    Q_OBJECT

    FakeHttpServer yandexApi;
    FakeHttpServer soundcloudApi;
    FakeTools tools;
    QTemporaryDir dir;

    QString ytDlpCalls() const {
        QFile log(dir.filePath("yt-dlp.log"));
        // лога нет, значит yt-dlp ни разу не звали
        if (!log.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(log.readAll());
    }

    // трек id есть в базе Яндекса, но не отдаётся
    void serveRemoved(int id, const QString& artist, const QString& title, int durationMs = 200000) {
        QString path = QString("/tracks/%1").arg(id);
        yandexApi.on("GET", path + "/download-info", [](const HttpRequest&) { return HttpResponse{404, "{}"}; });
        yandexApi.on("GET", path, [=](const HttpRequest&) {
            QJsonObject track{{"id", id},
                              {"title", title},
                              {"durationMs", durationMs},
                              {"artists", QJsonArray{QJsonObject{{"name", artist}}}}};
            return jsonResult(QJsonArray{track});
        });
    }

    // поиск на SoundCloud отдаёт found, поток есть у любого найденного трека
    void serveSoundCloud(const QJsonArray& found) {
        QString base = soundcloudApi.url();
        soundcloudApi.on("GET", "/search/tracks",
                         [found](const HttpRequest&) { return jsonBody({{"collection", found}}); });
        for (const auto& t : found) {
            QString id = t.toObject()["id"].toVariant().toString();
            soundcloudApi.on("GET", "/tracks/" + id, [base, id](const HttpRequest&) {
                QJsonObject transcoding{{"url", base + "/media/" + id},
                                        {"format", QJsonObject{{"protocol", "progressive"}}}};
                return jsonBody({{"media", QJsonObject{{"transcodings", QJsonArray{transcoding}}}}});
            });
            soundcloudApi.on("GET", "/media/" + id, [id](const HttpRequest&) {
                return jsonBody({{"url", "https://sc.example/" + id + ".mp3"}});
            });
        }
    }

    static QJsonObject soundcloudTrack(int id, const QString& user, const QString& title, int durationMs = 200000) {
        return {{"id", id}, {"title", title}, {"duration", durationMs}, {"user", QJsonObject{{"username", user}}}};
    }

    // свои варианты для поиска на YouTube вместо того, что скрипт собирает из запроса
    void youtubeCandidates(const QList<QJsonObject>& candidates) {
        QFile file(dir.filePath("candidates.jsonl"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (const QJsonObject& c : candidates) file.write(QJsonDocument(c).toJson(QJsonDocument::Compact) + "\n");
        qputenv("YTDLP_CANDIDATES", file.fileName().toUtf8());
    }

    static QVariantMap yandexTrack(const QString& id, const QString& artist, const QString& title) {
        return {{"id", id}, {"service", "Yandex"}, {"artist", artist}, {"title", title}, {"durationMs", 200000}};
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("MORPH_YANDEX_API", yandexApi.url().toUtf8());
        qputenv("MORPH_SOUNDCLOUD_API", soundcloudApi.url().toUtf8());
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        // Поиск вариантов (ytsearch5) по умолчанию находит клип vid1, названный
        // ровно как запрос, длиной 200 с. С NOPE в запросе не находит ничего,
        // клип из YTDLP_BROKEN не отдаёт поток
        tools.add("yt-dlp",
                  "echo \"$@\" >> \"$YTDLP_LOG\"\n"
                  "for last; do :; done\n"
                  "case \"$last\" in\n"
                  "  ytsearch5:*NOPE*) exit 0 ;;\n"
                  "  ytsearch5:*)\n"
                  "    if [ -n \"$YTDLP_CANDIDATES\" ]; then cat \"$YTDLP_CANDIDATES\"; exit 0; fi\n"
                  "    echo \"{\\\"id\\\": \\\"vid1\\\", \\\"title\\\": \\\"${last#ytsearch5:}\\\", \\\"channel\\\": "
                  "\\\"Uploader\\\", \\\"duration\\\": 200}\"\n"
                  "    exit 0 ;;\n"
                  "esac\n"
                  "if [ -n \"$YTDLP_BROKEN\" ]; then case \"$last\" in *\"$YTDLP_BROKEN\"*) exit 1 ;; esac; fi\n"
                  "echo \"{\\\"url\\\": \\\"${YTDLP_URL:-https://cdn.example/found.m4a}\\\", \\\"abr\\\": 129.5}\"\n");
    }

    void init() {
        QFile::remove(dir.filePath("yt-dlp.log"));
        QFile::remove(PathProvider::getDataPath() + "/fallback_choices.json");
        qunsetenv("YTDLP_URL");
        qunsetenv("YTDLP_CANDIDATES");
        qunsetenv("YTDLP_BROKEN");
        soundcloudApi.reset();
    }

    void removedYandexTrackPlaysFromYouTube() {
        serveRemoved(101, "Ежемесячные", "Молотилка");

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy bitrates(&services, &ServiceManager::bitrateReady);
        QSignalSpy chosen(&services, &ServiceManager::fallbackChosen);

        services.resolve("Yandex", "101");
        QVERIFY(streams.wait());

        QCOMPARE(streams[0][0].toString(), QString("101"));
        QCOMPARE(streams[0][1].toString(), QString("https://cdn.example/found.m4a"));
        QCOMPARE(bitrates.last()[1].toInt(), 130);
        QVERIFY(ytDlpCalls().contains("ytsearch5:Ежемесячные - Молотилка"));
        QVERIFY(ytDlpCalls().contains("https://www.youtube.com/watch?v=vid1"));
        QVERIFY(ytDlpCalls().contains("bestaudio[ext=m4a]/bestaudio"));

        QVariantMap source = chosen[0][1].toMap();
        QCOMPARE(source["service"].toString(), QString("YouTube"));
        QCOMPARE(source["index"].toInt(), 1);
        QCOMPARE(source["total"].toInt(), 1);
    }

    void textListBecomesPlayableYouTubePlaylist() {
        QFile list(dir.filePath("missing.txt"));
        QVERIFY(list.open(QIODevice::WriteOnly));
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
        QVERIFY(ytDlpCalls().contains("ytsearch5:Zamay - Неошансон"));
    }

    void queryIdRoundTrip() {
        QString query = "Слава КПСС - Эллен Шейдлин / remix?";
        QString id = YouTubeService::queryTrackId(query);
        QCOMPARE(YouTubeService::queryFromTrackId(id), query);
        QVERIFY(YouTubeService::queryFromTrackId("dQw4w9WgXcQ").isEmpty());
    }

    void emptyFileIsAnError() {
        QFile list(dir.filePath("empty.txt"));
        QVERIFY(list.open(QIODevice::WriteOnly));
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

    // SoundCloud без VPN и одним запросом: если там та же песня той же длины, YouTube не нужен
    void soundCloudFirstWhenItIsSure() {
        serveRemoved(303, "Rare", "Song");
        serveSoundCloud({soundcloudTrack(42, "Rare", "Song")});

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
        QCOMPARE(soundcloudApi.requestsTo("/search/tracks").last().query.queryItemValue("q"), QString("Rare - Song"));
        QVERIFY(ytDlpCalls().isEmpty());
    }

    // на SoundCloud только лайв, на YouTube студийная версия: играет она
    void youTubeWhenSoundCloudIsUnsure() {
        serveRemoved(304, "Rare", "Song");
        serveSoundCloud({soundcloudTrack(43, "Rare", "Song (Live)", 260000)});

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy chosen(&services, &ServiceManager::fallbackChosen);

        services.resolve("Yandex", "304");
        QVERIFY(streams.wait());
        QCOMPARE(streams[0][1].toString(), QString("https://cdn.example/found.m4a"));
        QCOMPARE(chosen[0][1].toMap()["id"].toString(), QString("vid1"));
    }

    void nowhereToBeFoundIsReported() {
        serveRemoved(404, "Ghost", "NOPE");
        serveSoundCloud({});

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy errors(&services, &ServiceManager::errorOccurred);

        services.resolve("Yandex", "404");
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(errors[0][0].toString().contains("Ghost - NOPE"));
    }

    // второй раз тот же трек ищется без поиска: сразу запомненный клип
    void choiceIsRemembered() {
        serveRemoved(505, "Kept", "Choice");
        {
            NetworkManager net;
            CacheManager cache(&net);
            ServiceManager services(&net, &cache);
            services.setYandexToken("token");
            QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
            services.resolve("Yandex", "505");
            QVERIFY(streams.wait());
        }
        QFile::remove(dir.filePath("yt-dlp.log"));

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        services.resolve("Yandex", "505");
        QVERIFY(streams.wait());
        QVERIFY(ytDlpCalls().contains("watch?v=vid1"));
        QVERIFY(!ytDlpCalls().contains("ytsearch5:"));
    }

    void otherVersionTakesTheNextCandidate() {
        serveRemoved(606, "Two", "Takes");
        youtubeCandidates({{{"id", "best"}, {"title", "Two - Takes"}, {"duration", 200}},
                           {{"id", "second"}, {"title", "Two - Takes"}, {"duration", 208}}});

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy chosen(&services, &ServiceManager::fallbackChosen);

        services.resolve("Yandex", "606");
        QVERIFY(streams.wait());
        QCOMPARE(chosen.last()[1].toMap()["id"].toString(), QString("best"));

        QVariantMap track = yandexTrack("606", "Two", "Takes");
        services.otherVersion(track);
        QTRY_COMPARE(streams.size(), 2);
        QVariantMap source = chosen.last()[1].toMap();
        QCOMPARE(source["id"].toString(), QString("second"));
        QCOMPARE(source["index"].toInt(), 2);
        QCOMPARE(source["total"].toInt(), 2);

        // выбор пережил перезапуск
        ServiceManager restarted(&net, &cache);
        QCOMPARE(restarted.fallbackSource(track)["id"].toString(), QString("second"));
        QVERIFY(restarted.fallbackSource({{"id", "dQw4w9WgXcQ"}, {"service", "YouTube"}}).isEmpty());
    }

    // запомненный клип удалили с YouTube: поиск заново
    void brokenRememberedChoiceSearchesAgain() {
        serveRemoved(707, "Gone", "Clip");
        {
            NetworkManager net;
            CacheManager cache(&net);
            ServiceManager services(&net, &cache);
            services.setYandexToken("token");
            QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
            services.resolve("Yandex", "707");
            QVERIFY(streams.wait());
        }
        qputenv("YTDLP_BROKEN", "vid1");
        youtubeCandidates({{{"id", "fresh"}, {"title", "Gone - Clip"}, {"duration", 200}}});

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy chosen(&services, &ServiceManager::fallbackChosen);
        services.resolve("Yandex", "707");
        QVERIFY(streams.wait(10000));
        QCOMPARE(chosen.last()[1].toMap()["id"].toString(), QString("fresh"));
    }

    // следующий трек нашёлся заранее и сразу скачан: потом он стартует с диска
    void prefetchedRemovedTrackIsDownloadedAhead() {
        serveRemoved(808, "Early", "Ahead");
        yandexApi.on("GET", "/googlevideo.com/808.m4a",
                     [](const HttpRequest&) { return HttpResponse{200, QByteArray(8192, 'a')}; });
        qputenv("YTDLP_URL", (yandexApi.url() + "/googlevideo.com/808.m4a").toUtf8());

        NetworkManager net;
        CacheManager cache(&net);
        cache.clearTrackCache();
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        QSignalSpy streams(&services, &ServiceManager::streamUrlReady);
        QSignalSpy cached(&cache, &CacheManager::trackCached);

        services.prefetch("Yandex", "808");
        QVERIFY(streams.wait());
        QVERIFY(!cached.isEmpty() || cached.wait());
        QCOMPARE(cached[0][0].toString(), QString("808"));
        QVERIFY(cache.isTrackCached("808"));
    }

    // заранее искали и не нашли: молчим, а когда трек включат, скажем
    void prefetchMissIsQuietUntilPlayed() {
        serveRemoved(909, "Quiet", "NOPE");
        serveSoundCloud({});

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("token");
        services.setSoundCloudClientId("client");
        QSignalSpy errors(&services, &ServiceManager::errorOccurred);

        services.prefetch("Yandex", "909");
        QVERIFY(soundcloudApi.waitFor("/search/tracks"));
        QTest::qWait(300);
        QCOMPARE(errors.size(), 0);

        services.resolve("Yandex", "909");
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(errors[0][0].toString().contains("Quiet - NOPE"));
    }
};

QTEST_GUILESS_MAIN(TestFallback)
#include "tst_fallback.moc"
