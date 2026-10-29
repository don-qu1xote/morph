#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

#include "services/YandexService.h"
#include "support/FakeHttpServer.h"

class TestYandexService : public QObject {
    Q_OBJECT

    FakeHttpServer api;

    static QJsonObject track(int id, int albumId, const QString& title, int durationMs = 200000) {
        return {{"id", id},
                {"title", title},
                {"durationMs", durationMs},
                {"artists", QJsonArray{QJsonObject{{"name", "Artist"}}}},
                {"albums", QJsonArray{QJsonObject{{"id", albumId}, {"title", "Album " + title}}}}};
    }

    // трек попадает в сервис так же, как в жизни: через ответ API
    void loadTracks(YandexService& yandex, const QJsonArray& tracks) {
        api.on("GET", "/search", [tracks](const HttpRequest&) {
            return jsonResult(QJsonObject{{"tracks", QJsonObject{{"results", tracks}}}});
        });
        QSignalSpy results(&yandex, &BaseService::searchResultsReady);
        yandex.search("seed");
        QVERIFY(results.wait());
    }

    // сессия s1 с двумя треками, партия b1
    void serveWave() {
        api.on("POST", "/rotor/session/new", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"radioSessionId", "s1"},
                                          {"batchId", "b1"},
                                          {"sequence", QJsonArray{QJsonObject{{"track", track(1, 10, "One")}},
                                                                  QJsonObject{{"track", track(2, 20, "Two")}}}}});
        });
    }

private slots:
    void initTestCase() { qputenv("MORPH_YANDEX_API", api.url().toUtf8()); }
    void init() { api.reset(); }

    void listenReportsRealSecondsAndAlbumId() {
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {track(101, 555, "Song", 215000)});

        yandex.reportPlay("101", "Album Song");
        yandex.reportListen("101", 42.4, 0, false);
        QVERIFY(api.waitFor("/play-audio"));

        QUrlQuery form = api.requestsTo("/play-audio").last().form();
        QCOMPARE(form.queryItemValue("track-id"), QString("101"));
        QCOMPARE(form.queryItemValue("album-id"), QString("555"));
        QCOMPARE(form.queryItemValue("total-played-seconds"), QString("42.4"));
        QCOMPARE(form.queryItemValue("track-length-seconds"), QString("215.0"));
        QCOMPARE(form.queryItemValue("end-position-seconds"), QString("42.4"));
    }

    void completedListenEndsAtTrackLength() {
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");

        yandex.reportListen("7", 180, 181.5, true);
        QVERIFY(api.waitFor("/play-audio"));

        QUrlQuery form = api.requestsTo("/play-audio").last().form();
        QCOMPARE(form.queryItemValue("end-position-seconds"), QString("181.5"));
        QVERIFY(!form.hasQueryItem("album-id"));
    }

    void startAloneSendsNothing() {
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");

        yandex.reportPlay("101", "");
        QTest::qWait(200);
        QVERIFY(api.requestsTo("/play-audio").isEmpty());
    }
    void waveStartsSessionAndReportsRadioStarted() {
        serveWave();

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.getWave();
        QVERIFY(wave.wait());
        QCOMPARE(wave[0][1].toList().size(), 2);

        HttpRequest start = api.requestsTo("/rotor/session/new").last();
        QCOMPARE(start.json()["seeds"].toArray(), QJsonArray{"user:onyourwave"});

        QVERIFY(api.waitFor("/rotor/session/s1/feedback"));
        QJsonObject feedback = api.requestsTo("/rotor/session/s1/feedback").last().json();
        QCOMPARE(feedback["event"].toObject()["type"].toString(), QString("radioStarted"));
        QCOMPARE(feedback["batchId"].toString(), QString("b1"));
    }

    void waveTrackEventsCarryRealSeconds() {
        serveWave();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());
        QVERIFY(api.waitFor("/rotor/session/s1/feedback"));

        yandex.reportPlay("1", "");
        yandex.reportListen("1", 12.5, 0, false);
        yandex.reportPlay("2", "");
        yandex.reportListen("2", 199, 200, true);
        QVERIFY(api.waitFor("/rotor/session/s1/feedback", 5));

        QList<HttpRequest> events = api.requestsTo("/rotor/session/s1/feedback").mid(1);
        QStringList types;
        for (const HttpRequest& r : events) types << r.json()["event"].toObject()["type"].toString();
        QCOMPARE(types, QStringList({"trackStarted", "skip", "trackStarted", "trackFinished"}));

        QJsonObject skip = events[1].json()["event"].toObject();
        QCOMPARE(skip["trackId"].toString(), QString("1:10"));
        QCOMPARE(skip["totalPlayedSeconds"].toDouble(), 12.5);
        QCOMPARE(events[1].json()["batchId"].toString(), QString("b1"));
    }

    void nextBatchSendsQueueOfIssuedTracks() {
        serveWave();
        api.on("POST", "/rotor/session/s1/tracks", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"batchId", "b2"},
                                          {"sequence", QJsonArray{QJsonObject{{"track", track(3, 30, "Three")}}}}});
        });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());
        yandex.getWave();
        QVERIFY(wave.wait());

        QJsonArray queue = api.requestsTo("/rotor/session/s1/tracks").last().json()["queue"].toArray();
        QCOMPARE(queue, QJsonArray({"1:10", "2:20"}));

        yandex.reportPlay("3", "");
        QVERIFY(api.waitFor("/rotor/session/s1/feedback", 2));
        QCOMPARE(api.requestsTo("/rotor/session/s1/feedback").last().json()["batchId"].toString(), QString("b2"));
    }

    void expiredSessionStartsNewOne() {
        serveWave();
        api.on("POST", "/rotor/session/s1/tracks",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"unknownSession", true}}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());
        yandex.getWave();
        QVERIFY(wave.wait());

        QCOMPARE(api.requestsTo("/rotor/session/new").size(), 2);
        QCOMPARE(api.requestsTo("/rotor/session/new").last().json()["queue"].toArray().size(), 2);
    }

    void parallelRequestsMakeOneSession() {
        serveWave();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.getWave();
        yandex.getWave();
        yandex.getWave();
        QVERIFY(wave.wait());
        QTest::qWait(100);
        QCOMPARE(api.requestsTo("/rotor/session/new").size(), 1);
    }

    void brokenSessionFallsBackToStationWithoutFeedback() {
        api.on("POST", "/rotor/session/new", [](const HttpRequest&) { return HttpResponse{500, "{}"}; });
        api.on("GET", "/rotor/station/user:onyourwave/tracks", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"sequence", QJsonArray{QJsonObject{{"track", track(9, 90, "Nine")}}}}});
        });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());
        QCOMPARE(wave[0][1].toList().size(), 1);

        yandex.reportPlay("9", "");
        QTest::qWait(150);
        for (const HttpRequest& r : api.requests) QVERIFY(!r.path.endsWith("/feedback"));
    }

    void searchTrackGetsNoWaveFeedback() {
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {track(5, 50, "Five")});

        yandex.reportPlay("5", "");
        QTest::qWait(150);
        for (const HttpRequest& r : api.requests) QVERIFY(!r.path.endsWith("/feedback"));
    }
    // лайки выдаются вперемешку по времени, а 300 штук не влезают в одну пачку /tracks
    void fetchLikesSortsNewestFirstInChunks() {
        api.on("GET", "/account/status",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 77}}}}); });
        api.on("GET", "/users/77/likes/tracks", [](const HttpRequest&) {
            QJsonArray shortTracks;
            for (int i = 1; i <= 300; ++i) {
                shortTracks.append(QJsonObject{{"id", QString::number(i)},
                                               {"albumId", QString::number(i * 10)},
                                               {"timestamp", QString("2026-%1").arg(i, 4, 10, QChar('0'))}});
            }
            return jsonResult(QJsonObject{{"library", QJsonObject{{"uid", 77}, {"tracks", shortTracks}}}});
        });
        api.on("POST", "/tracks", [](const HttpRequest& r) {
            QJsonArray tracks;
            for (const QString& fullId : r.form().queryItemValue("track-ids").split(',')) {
                int id = fullId.section(':', 0, 0).toInt();
                if (id == 150) continue; // убран из каталога
                tracks.prepend(track(id, id * 10, "t" + QString::number(id)));
            }
            return jsonResult(tracks);
        });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy fetched(&yandex, &YandexService::likesFetched);

        yandex.fetchLikes();
        QVERIFY(fetched.wait());

        QCOMPARE(api.requestsTo("/tracks").size(), 2);
        QVERIFY(api.requestsTo("/tracks").first().form().queryItemValue("track-ids").startsWith("300:3000,299:2990"));

        QVariantList tracks = fetched[0][0].toList();
        QStringList allIds = fetched[0][1].toStringList();
        QCOMPARE(tracks.size(), 299);
        QCOMPARE(allIds.size(), 300);
        QCOMPARE(tracks.first().toMap()["id"].toString(), QString("300"));
        QCOMPARE(tracks.last().toMap()["id"].toString(), QString("1"));
        QVERIFY(allIds.contains("150"));
    }

    void likeAndUnlikeUseAlbumAwareIds() {
        api.on("GET", "/account/status",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 77}}}}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {track(101, 555, "Song")});

        yandex.setLiked("101", true);
        yandex.setLiked("101", false);
        QVERIFY(api.waitFor("/users/77/likes/tracks/add-multiple"));
        QVERIFY(api.waitFor("/users/77/likes/tracks/remove"));

        QCOMPARE(api.requestsTo("/users/77/likes/tracks/add-multiple").last().form().queryItemValue("track-ids"),
                 QString("101:555"));
        // uid спрашивается один раз
        QCOMPARE(api.requestsTo("/account/status").size(), 1);
    }

    void likeInsideWaveIsAlsoWaveFeedback() {
        serveWave();
        api.on("GET", "/account/status",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 77}}}}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());

        yandex.setLiked("2", true);
        QVERIFY(api.waitFor("/rotor/session/s1/feedback", 2));
        QJsonObject event = api.requestsTo("/rotor/session/s1/feedback").last().json()["event"].toObject();
        QCOMPARE(event["type"].toString(), QString("like"));
        QCOMPARE(event["trackId"].toString(), QString("2:20"));
    }
    void refusedDownloadMeansUnavailable() {
        api.on("GET", "/tracks/101/download-info", [](const HttpRequest&) { return HttpResponse{404, "{}"}; });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {track(101, 555, "Song")});
        QSignalSpy unavailable(&yandex, &YandexService::streamUnavailable);
        QSignalSpy errors(&yandex, &BaseService::errorOccurred);

        yandex.resolveStreamUrl("101");
        QVERIFY(unavailable.wait());
        QCOMPARE(unavailable[0][0].toString(), QString("101"));
        QCOMPARE(unavailable[0][1].toString(), QString("Artist - Song"));
        QCOMPARE(errors.count(), 0);
    }

    void emptyDownloadInfoMeansUnavailable() {
        api.on("GET", "/tracks/101/download-info", [](const HttpRequest&) { return jsonResult(QJsonArray{}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {track(101, 555, "Song")});
        QSignalSpy unavailable(&yandex, &YandexService::streamUnavailable);

        yandex.resolveStreamUrl("101");
        QVERIFY(unavailable.wait());
    }

    void trackMarkedUnavailableSkipsDownloadInfo() {
        QJsonObject removed = track(101, 555, "Song");
        removed["available"] = false;

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        loadTracks(yandex, {removed});
        QSignalSpy unavailable(&yandex, &YandexService::streamUnavailable);

        yandex.resolveStreamUrl("101");
        QCOMPARE(unavailable.count(), 1);
        QVERIFY(api.requestsTo("/tracks/101/download-info").isEmpty());
    }

    void unknownTrackIsLookedUpByIdFirst() {
        api.on("GET", "/tracks/202/download-info", [](const HttpRequest&) { return HttpResponse{451, "{}"}; });
        api.on("GET", "/tracks/202", [](const HttpRequest&) { return jsonResult(QJsonArray{track(202, 9, "Lost")}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy unavailable(&yandex, &YandexService::streamUnavailable);

        yandex.resolveStreamUrl("202");
        QVERIFY(unavailable.wait());
        QCOMPARE(unavailable[0][1].toString(), QString("Artist - Lost"));
    }

    void noNetworkIsAnErrorNotAFallback() {
        qputenv("MORPH_YANDEX_API", "http://127.0.0.1:1");
        NetworkManager net;
        YandexService yandex(&net);
        qputenv("MORPH_YANDEX_API", api.url().toUtf8());
        yandex.setToken("token");
        QSignalSpy unavailable(&yandex, &YandexService::streamUnavailable);
        QSignalSpy errors(&yandex, &BaseService::errorOccurred);

        yandex.resolveStreamUrl("101");
        QVERIFY(errors.wait());
        QCOMPARE(unavailable.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestYandexService)
#include "tst_yandex_service.moc"
