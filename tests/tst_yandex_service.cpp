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

    // Фидбек уходит отдельными запросами, и до сервера они доходят в любом порядке:
    // QNetworkAccessManager держит несколько соединений сразу. Поэтому событие
    // ищем по типу и треку, а не по позиции в списке
    QJsonObject feedback(const QString& type, const QString& trackId = QString()) const {
        for (const HttpRequest& r : api.requestsTo("/rotor/session/s1/feedback")) {
            QJsonObject body = r.json();
            QJsonObject event = body["event"].toObject();
            if (event["type"].toString() == type && (trackId.isEmpty() || event["trackId"].toString() == trackId)) {
                return body;
            }
        }
        return {};
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
        QCOMPARE(feedback("radioStarted")["batchId"].toString(), QString("b1"));
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
        QTest::qWait(100);
        QCOMPARE(api.requestsTo("/rotor/session/s1/feedback").size(), 5);

        QVERIFY(!feedback("trackStarted", "1:10").isEmpty());
        QVERIFY(!feedback("trackStarted", "2:20").isEmpty());

        QJsonObject skip = feedback("skip", "1:10");
        QCOMPARE(skip["event"].toObject()["totalPlayedSeconds"].toDouble(), 12.5);
        QCOMPARE(skip["batchId"].toString(), QString("b1"));

        QJsonObject finished = feedback("trackFinished", "2:20");
        QCOMPARE(finished["event"].toObject()["totalPlayedSeconds"].toDouble(), 199.0);
        QVERIFY(feedback("skip", "2:20").isEmpty());
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
        QCOMPARE(feedback("trackStarted", "3:30")["batchId"].toString(), QString("b2"));
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
        QVERIFY(!feedback("like", "2:20").isEmpty());
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
        QCOMPARE(unavailable[0][1].toString(), QString("Artist"));
        QCOMPARE(unavailable[0][2].toString(), QString("Song"));
        QCOMPARE(unavailable[0][3].toLongLong(), 200000);
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
        QCOMPARE(unavailable[0][2].toString(), QString("Lost"));
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
    void dislikeHitsCollectionAndWave() {
        serveWave();
        api.on("GET", "/account/status",
               [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 77}}}}); });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);
        yandex.getWave();
        QVERIFY(wave.wait());

        yandex.dislike("1");
        QVERIFY(api.waitFor("/users/77/dislikes/tracks/add-multiple"));
        QCOMPARE(api.requestsTo("/users/77/dislikes/tracks/add-multiple")[0].form().queryItemValue("track-ids"),
                 QString("1:10"));

        QVERIFY(api.waitFor("/rotor/session/s1/feedback", 2));
        QVERIFY(!feedback("dislike", "1:10").isEmpty());
    }

    // сессия на каждый запрос своя, трек в партии называется по первому сиду
    void serveSessionsBySeed() {
        api.on("POST", "/rotor/session/new", [](const HttpRequest& r) {
            QString seed = r.json()["seeds"].toArray().first().toString();
            static int n = 0;
            ++n;
            return jsonResult(QJsonObject{{"radioSessionId", QString("s%1").arg(n)},
                                          {"batchId", "b"},
                                          {"sequence", QJsonArray{QJsonObject{{"track", track(n, 10, seed)}}}}});
        });
    }

    void stationWaveUsesItsSeeds() {
        serveSessionsBySeed();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        yandex.setWaveSettings({"settingDiversity:discover"});
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.startWave({"genre:rap"});
        QVERIFY(wave.wait());
        QCOMPARE(api.requestsTo("/rotor/session/new").last().json()["seeds"].toArray(), QJsonArray{"genre:rap"});
    }

    // настройки Моей волны едут сидами в новую сессию, а смена настроек её пересоздаёт
    void myWaveSettingsBecomeSeeds() {
        serveSessionsBySeed();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.getWave();
        QVERIFY(wave.wait());
        yandex.setWaveSettings({"settingDiversity:discover", "settingLanguage:russian"});
        yandex.getWave();
        QVERIFY(wave.wait());

        QList<HttpRequest> sessions = api.requestsTo("/rotor/session/new");
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].json()["seeds"].toArray(), QJsonArray{"user:onyourwave"});
        QCOMPARE(sessions[1].json()["seeds"].toArray(),
                 QJsonArray({"user:onyourwave", "settingDiversity:discover", "settingLanguage:russian"}));
    }

    // переключились на другую станцию, пока старая отвечала: старая партия не всплывает
    void switchingStationsDropsTheOldBatch() {
        serveSessionsBySeed();
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.getWave();
        yandex.startWave({"activity:workout"});
        QVERIFY(wave.wait());
        QTest::qWait(200);

        QCOMPARE(wave.size(), 1);
        QCOMPARE(wave[0][1].toList().first().toMap()["title"].toString(), QString("activity:workout"));
    }

    void foreignTrackWaveFindsTheTrackOnYandex() {
        serveSessionsBySeed();
        api.on("GET", "/search", [](const HttpRequest& r) {
            QJsonObject found{{"results", QJsonArray{QJsonObject{{"id", 77}}}}};
            return jsonResult(QJsonObject{{r.query.queryItemValue("type") + "s", found}});
        });
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy wave(&yandex, &BaseService::waveReady);

        yandex.startTrackWave({{"service", "YouTube"}, {"id", "abc"}, {"artist", "Zamay"}, {"title", "Неошансон"}});
        QVERIFY(wave.wait());
        HttpRequest search = api.requestsTo("/search").last();
        QCOMPARE(search.query.queryItemValue("text"), QString("Zamay - Неошансон"));
        QCOMPARE(api.requestsTo("/rotor/session/new").last().json()["seeds"].toArray(), QJsonArray{"track:77"});

        yandex.startArtistWave("Zamay");
        QVERIFY(wave.wait());
        QCOMPARE(api.requestsTo("/search").last().query.queryItemValue("type"), QString("artist"));
        QCOMPARE(api.requestsTo("/rotor/session/new").last().json()["seeds"].toArray(), QJsonArray{"artist:77"});
    }

    void stationsAndSettingsForTheHomeScreen() {
        QJsonObject workout{
            {"id", QJsonObject{{"type", "activity"}, {"tag", "workout"}}},
            {"name", "Тренировка"},
            {"icon", QJsonObject{{"backgroundColor", "#ff5c00"}, {"imageUrl", "avatars.example/run/%%"}}}};
        QJsonObject rap{{"id", QJsonObject{{"type", "genre"}, {"tag", "rap"}}},
                        {"name", "Рэп"},
                        {"icon", QJsonObject{{"backgroundColor", "#222222"}, {"imageUrl", "avatars.example/rap/%%"}}}};
        QJsonObject discover{
            {"value", "discover"}, {"name", "Незнакомое"}, {"serializedSeed", "settingDiversity:discover"}};
        api.on("GET", "/rotor/wave/settings", [=](const HttpRequest&) {
            QJsonObject restrictions{
                {"diversity",
                 QJsonObject{{"type", "enum"}, {"name", "Характер"}, {"possibleValues", QJsonArray{discover}}}}};
            return jsonResult(
                QJsonObject{{"blocks", QJsonArray{QJsonObject{{"type", "contexts"}, {"items", QJsonArray{workout}}}}},
                            {"settingRestrictions", restrictions}});
        });
        api.on("GET", "/rotor/stations/dashboard", [=](const HttpRequest&) {
            return jsonResult(QJsonObject{
                {"stations", QJsonArray{QJsonObject{{"station", rap}}, QJsonObject{{"station", workout}}}}});
        });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy stations(&yandex, &YandexService::stationsReady);
        QSignalSpy settings(&yandex, &YandexService::waveSettingsReady);

        yandex.fetchWaveStations();
        QVERIFY(stations.wait());

        QVariantList list = stations[0][0].toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].toMap()["seed"].toString(), QString("activity:workout"));
        QCOMPARE(list[0].toMap()["imageUrl"].toString(), QString("https://avatars.example/run/200x200"));
        QCOMPARE(list[1].toMap()["name"].toString(), QString("Рэп"));

        QVariantMap diversity = settings[0][0].toList().first().toMap();
        QCOMPARE(diversity["key"].toString(), QString("diversity"));
        QCOMPARE(diversity["values"].toList().first().toMap()["seed"].toString(), QString("settingDiversity:discover"));
    }

    void personalAndEditorialPlaylistsForTheHomeScreen() {
        auto personal = [](int kind, const QString& title, const QJsonObject& cover, bool ready = true) {
            QJsonObject playlist{{"uid", 5}, {"kind", kind}, {"title", title}, {"cover", cover}};
            return jsonResult(QJsonObject{{"ready", ready}, {"data", playlist}});
        };
        api.on("GET", "/playlists/personal/daily", [=](const HttpRequest&) {
            return personal(100, "Плейлист дня", QJsonObject{{"uri", "avatars.example/day/%%"}});
        });
        api.on("GET", "/playlists/personal/recentTracks",
               [=](const HttpRequest&) { return personal(101, "Премьера", {}, false); });
        api.on("GET", "/playlists/personal/missedLikes", [=](const HttpRequest&) {
            return personal(103, "Тайник", QJsonObject{{"itemsUri", QJsonArray{"avatars.example/m1/%%"}}});
        });
        api.on("GET", "/landing3", [](const HttpRequest&) {
            QJsonObject editorial{{"owner", QJsonObject{{"uid", 9}}},
                                  {"kind", 7},
                                  {"title", "Рэп-новинки"},
                                  {"ogImage", "avatars.example/og/%%"}};
            QJsonObject block{{"type", "new-playlists"},
                              {"entities", QJsonArray{QJsonObject{{"type", "playlist"}, {"data", editorial}}}}};
            return jsonResult(QJsonObject{{"blocks", QJsonArray{block}}});
        });

        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("token");
        QSignalSpy mixes(&yandex, &BaseService::dailyMixesReady);

        yandex.getDailyMixes();
        QVERIFY(mixes.wait());

        QVariantList cards = mixes[0][1].toList();
        QCOMPARE(cards.size(), 3);
        QVariantMap day = cards[0].toMap();
        QCOMPARE(day["name"].toString(), QString("Плейлист дня"));
        QCOMPARE(day["coverUrl"].toString(), QString("https://avatars.example/day/400x400"));
        QCOMPARE(day["permalink_url"].toString(), QString("https://music.yandex.ru/users/5/playlists/100"));
        QCOMPARE(cards[1].toMap()["coverUrl"].toString(), QString("https://avatars.example/m1/400x400"));
        QVariantMap editorial = cards[2].toMap();
        QCOMPARE(editorial["id"].toString(), QString("9:7"));
        QCOMPARE(editorial["coverUrl"].toString(), QString("https://avatars.example/og/400x400"));
    }
};

QTEST_GUILESS_MAIN(TestYandexService)
#include "tst_yandex_service.moc"
