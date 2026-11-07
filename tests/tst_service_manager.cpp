#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/ServiceManager.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"
#include "utils/PathProvider.h"

class TestServiceManager : public QObject {
    Q_OBJECT

    FakeHttpServer yandexApi;
    FakeHttpServer soundcloudApi;
    FakeTools tools;

    // поиск в Яндексе всегда находит «Zamay - Неошансон», id 31 в альбоме 9
    void serveZamaySearch() {
        yandexApi.on("GET", "/account/status",
                     [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });
        yandexApi.on("GET", "/search", [](const HttpRequest&) {
            QJsonObject track{{"id", 31},
                              {"title", "Неошансон"},
                              {"durationMs", 187000},
                              {"artists", QJsonArray{QJsonObject{{"name", "Zamay"}}}},
                              {"albums", QJsonArray{QJsonObject{{"id", 9}, {"title", "Album"}}}}};
            return jsonResult(QJsonObject{{"tracks", QJsonObject{{"results", QJsonArray{track}}}}});
        });
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Тесты ждут пустую папку данных, как на CI. Локально в ней копится
        // всё от прошлых запусков: библиотека, история, кэш треков
        QDir(PathProvider::getDataPath()).removeRecursively();
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

    // Лайк с SoundCloud находит ту же песню в Яндексе. С синхронизацией лайк,
    // прослушивание и снятый лайк уходят и туда
    void soundCloudLikeIsLinkedAndSyncedToYandex() {
        QFile::remove(PathProvider::getDataPath() + "/library.json");
        serveZamaySearch();

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("e");
        services.setSyncToYandex(true);
        yandexApi.requests.clear();

        QVariantMap liked{{"id", "s1"},
                          {"service", "SoundCloud"},
                          {"artist", "uploader"},
                          {"title", "Zamay - Неошансон"},
                          {"durationMs", 188000}};
        services.library()->syncLikes({liked});
        QVERIFY(yandexApi.waitFor("/users/5/likes/tracks/add-multiple"));
        QCOMPARE(yandexApi.requestsTo("/search").last().query.queryItemValue("text"), QString("Zamay Неошансон"));
        QCOMPARE(yandexApi.requestsTo("/users/5/likes/tracks/add-multiple")[0].form().queryItemValue("track-ids"),
                 QString("31:9"));
        QCOMPARE(services.library()->sourcesOf("s1"), QStringList({"SoundCloud", "Yandex"}));

        services.reportListen("SoundCloud", "s1", "", 180, 188, true);
        QVERIFY(yandexApi.waitFor("/play-audio"));
        QCOMPARE(yandexApi.requestsTo("/play-audio")[0].form().queryItemValue("track-id"), QString("31"));

        services.library()->syncLikes({});
        services.setLiked(liked, false);
        QVERIFY(yandexApi.waitFor("/users/5/likes/tracks/remove"));
    }

    // к настройкам Яндекса добавляется своя: доля незнакомых артистов
    void discoveryOptionJoinsYandexSettings() {
        yandexApi.on("GET", "/rotor/wave/settings", [](const HttpRequest&) {
            QJsonObject value{{"name", "Незнакомое"}, {"serializedSeed", "settingDiversity:discover"}};
            QJsonObject restrictions{
                {"diversity", QJsonObject{{"name", "Характер"}, {"possibleValues", QJsonArray{value}}}}};
            return jsonResult(QJsonObject{{"settingRestrictions", restrictions}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("g");
        QSignalSpy settings(&services, &ServiceManager::waveSettingsReady);

        services.fetchWaveStations();
        QVERIFY(settings.wait());
        QVariantList options = settings[0][0].toList();
        QCOMPARE(options.size(), 2);
        QCOMPARE(options[0].toMap()["key"].toString(), QString("diversity"));
        QVariantMap morph = options[1].toMap();
        QCOMPARE(morph["key"].toString(), QString("morphDiscovery"));
        QCOMPARE(morph["values"].toList()[2].toMap()["seed"].toString(), QString("morph:new"));

        services.setDiscovery("new");
        QCOMPARE(services.recommender()->newShare(), 0.5);
        services.setDiscovery("");
        QCOMPARE(services.recommender()->newShare(), 0.3);
    }

    // Страница артиста из brief-info: популярное, альбомы, похожие. Нажали на
    // гостя, а не на первого артиста трека: id ищется поиском по имени
    void artistPageComesFromYandex() {
        QFile::remove(PathProvider::getDataPath() + "/library.json");
        yandexApi.on("GET", "/search", [](const HttpRequest& r) {
            if (r.query.queryItemValue("type") != "artist") return jsonResult(QJsonObject{});
            return jsonResult(QJsonObject{{"artists", QJsonObject{{"results", QJsonArray{QJsonObject{{"id", 77}}}}}}});
        });
        yandexApi.on("GET", "/artists/77/brief-info", [](const HttpRequest&) {
            QJsonObject artist{{"id", 77},
                               {"name", "Guest"},
                               {"genres", QJsonArray{"rusrap"}},
                               {"cover", QJsonObject{{"uri", "avatars.example/g/%%"}}}};
            QJsonObject popular{{"id", 7},
                                {"title", "Hit"},
                                {"durationMs", 180000},
                                {"artists", QJsonArray{QJsonObject{{"id", 77}, {"name", "Guest"}}}}};
            QJsonObject album{{"id", 5}, {"title", "LP"}, {"year", 2026}, {"coverUri", "c/%%"}};
            QJsonObject single{{"id", 6}, {"title", "New"}, {"year", 2026}, {"type", "single"}};
            return jsonResult(QJsonObject{{"artist", artist},
                                          {"popularTracks", QJsonArray{popular}},
                                          {"lastReleases", QJsonArray{single}},
                                          {"albums", QJsonArray{album, single}},
                                          {"similarArtists", QJsonArray{QJsonObject{{"id", 78}, {"name", "Friend"}}}},
                                          {"stats", QJsonObject{{"lastMonthListeners", 1200}}}});
        });
        yandexApi.on("GET", "/albums/5/with-tracks", [](const HttpRequest&) {
            QJsonObject t{{"id", 51}, {"title", "Intro"}, {"artists", QJsonArray{QJsonObject{{"name", "Guest"}}}}};
            // QJsonArray{QJsonArray{t}} в clang схлопывается в один массив, диск нужен отдельно
            QJsonArray volumes;
            volumes.append(QJsonArray{t});
            return jsonResult(QJsonObject{{"volumes", volumes}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("h");
        services.library()->syncLikes(
            {QVariantMap{{"id", "1"}, {"service", "Yandex"}, {"artist", "Guest"}, {"title", "Old"}}});
        QSignalSpy ready(&services, &ServiceManager::artistReady);

        QVariantMap track{{"id", "9"}, {"service", "Yandex"}, {"artist", "Lead, Guest"}, {"artistId", "70"}};
        services.fetchArtist("Guest", track);
        QVERIFY(ready.wait());
        QVariantMap info = ready[0][0].toMap();
        QCOMPARE(info["requestedName"].toString(), QString("Guest"));
        QCOMPARE(info["imageUrl"].toString(), QString("https://avatars.example/g/400x400"));
        QCOMPARE(info["genres"].toString(), QString("rusrap"));
        QCOMPARE(info["listeners"].toInt(), 1200);
        QCOMPARE(info["popular"].toList().size(), 1);
        QStringList albums;
        for (const QVariant& a : info["albums"].toList()) albums.append(a.toMap()["title"].toString());
        QCOMPARE(albums, QStringList({"New", "LP"}));
        QCOMPARE(info["similar"].toList()[0].toMap()["name"].toString(), QString("Friend"));
        QCOMPARE(info["inMain"].toInt(), 1);
        QVERIFY(yandexApi.requestsTo("/artists/70/brief-info").isEmpty());

        QSignalSpy imported(&services, &ServiceManager::playlistImported);
        services.openAlbum(info["albums"].toList()[1].toMap());
        QVERIFY(imported.wait());
        QCOMPARE(imported[0][0].toString(), QString("LP"));
        QCOMPARE(imported[0][2].toList().size(), 1);
    }

    // в Яндексе артиста нет: страница из того, что нашлось на SoundCloud
    void artistPageFallsBackToSoundCloud() {
        yandexApi.on("GET", "/search", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"artists", QJsonObject{{"results", QJsonArray{}}}}});
        });
        soundcloudApi.on("GET", "/search/tracks", [](const HttpRequest&) {
            QJsonArray found{
                QJsonObject{{"id", 1}, {"title", "Underground - Demo"}, {"user", QJsonObject{{"username", "x"}}}},
                QJsonObject{{"id", 2}, {"title", "Other - Song"}, {"user", QJsonObject{{"username", "y"}}}}};
            return jsonBody({{"collection", found}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("i");
        services.setSoundCloudClientId("client");
        QSignalSpy ready(&services, &ServiceManager::artistReady);

        services.fetchArtist("Underground", {});
        QVERIFY(ready.wait());
        QVariantMap info = ready[0][0].toMap();
        QCOMPARE(info["name"].toString(), QString("Underground"));
        QVariantList popular = info["popular"].toList();
        QCOMPARE(popular.size(), 1);
        QCOMPARE(popular[0].toMap()["id"].toString(), QString("1"));
    }

    void playlistSearchAndSort() {
        QFile::remove(PathProvider::getDataPath() + "/listening.jsonl");
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        auto t = [](const QString& id, const QString& artist, const QString& title, int seconds) {
            return QVariant(QVariantMap{{"id", id},
                                        {"service", "Yandex"},
                                        {"artist", artist},
                                        {"title", title},
                                        {"durationMs", seconds * 1000}});
        };
        QVariantList list{t("1", "Ёлка", "Трек 10", 200), t("2", "Zamay", "Трек 2", 150),
                          t("3", "Zamay", "Неошансон", 187)};
        auto ids = [](const QVariantList& tracks) {
            QStringList out;
            for (const QVariant& v : tracks) out.append(v.toMap()["id"].toString());
            return out;
        };

        QCOMPARE(ids(services.arrangeTracks(list, "", "custom")), QStringList({"1", "2", "3"}));
        QCOMPARE(ids(services.arrangeTracks(list, "zamay трек", "custom")), QStringList({"2"}));
        // ё ищется и через е
        QCOMPARE(ids(services.arrangeTracks(list, "елка", "custom")), QStringList({"1"}));
        QCOMPARE(ids(services.arrangeTracks(list, "", "title")).first(), QString("3"));
        // Порядок букв идёт от языка системы: в русском кириллица раньше
        // латиницы, в английском наоборот. Проверяются оба, а не язык машины
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        QCOMPARE(ids(services.arrangeTracks(list, "", "artist")).last(), QString("1"));
        QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
        QCOMPARE(ids(services.arrangeTracks(list, "", "artist")).first(), QString("1"));
        QLocale::setDefault(QLocale::system());
        QCOMPARE(ids(services.arrangeTracks(list, "", "duration")), QStringList({"2", "3", "1"}));

        services.nowPlaying(list[2].toMap());
        services.reportListen("Yandex", "3", "", 187, 187, true);
        QCOMPARE(ids(services.arrangeTracks(list, "", "plays")).first(), QString("3"));
    }

    // волна по плейлисту: похожее на его треки, дальше getWave просит её же
    void playlistWaveKeepsGoing() {
        yandexApi.on("GET", "/tracks/41/similar", [](const HttpRequest&) {
            QJsonArray similar;
            for (int i = 0; i < 6; ++i)
                similar.append(
                    QJsonObject{{"id", 700 + i},
                                {"title", "Similar " + QString::number(i)},
                                {"artists", QJsonArray{QJsonObject{{"name", "Near " + QString::number(i)}}}}});
            return jsonResult(QJsonObject{{"similarTracks", similar}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("j");
        QSignalSpy wave(&services, &ServiceManager::waveReady);

        QVariantMap seed{{"id", "41"}, {"service", "Yandex"}, {"artist", "Seed"}, {"title", "One"}};
        services.startPlaylistWave({seed});
        QVERIFY(wave.wait());
        QCOMPARE(wave[0][0].toString(), QString("morph"));
        QVERIFY(!wave[0][1].toList().isEmpty());

        int asked = yandexApi.requestsTo("/tracks/41/similar").size();
        services.getWave();
        QVERIFY(yandexApi.waitFor("/tracks/41/similar", asked + 1));
        QVERIFY(yandexApi.requestsTo("/rotor/session/new").isEmpty());
    }

    // в историю уходит сам трек, хотя ListenTracker знает только id
    void listenIsRecordedWithTheTrack() {
        QFile::remove(PathProvider::getDataPath() + "/listening.jsonl");
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QVariantMap track{{"id", "sc9"}, {"service", "SoundCloud"}, {"artist", "Zamay"}, {"title", "Song"}};

        services.nowPlaying(track);
        services.reportListen("SoundCloud", "sc9", "", 15, 200, false);
        services.dislike(track);

        const QList<ListenEvent>& events = services.recommender()->history().events();
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].track["artist"].toString(), QString("Zamay"));
        QVERIFY(events[0].skipped());
        QCOMPARE(events[1].kind, ListenEvent::Dislike);
    }

    // MAIN лайками на SoundCloud: версия ищется там, ставится лайк, а когда
    // песню убирают из MAIN, лайк снимается
    void mainIsMirroredToSoundCloud() {
        QFile::remove(PathProvider::getDataPath() + "/library.json");
        soundcloudApi.on("GET", "/me", [](const HttpRequest&) { return jsonBody({{"id", 555}}); });
        soundcloudApi.on("GET", "/search/tracks", [](const HttpRequest&) {
            QJsonObject found{{"id", 88},
                              {"title", "Zamay - Неошансон"},
                              {"duration", 187000},
                              {"user", QJsonObject{{"username", "uploader"}}}};
            return jsonBody({{"collection", QJsonArray{found}}});
        });
        soundcloudApi.on("PUT", "/users/555/track_likes/88",
                         [](const HttpRequest&) { return HttpResponse{200, "{}"}; });
        soundcloudApi.on("DELETE", "/users/555/track_likes/88",
                         [](const HttpRequest&) { return HttpResponse{200, "{}"}; });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setSoundCloudClientId("2-oauth");
        services.setSyncToSoundCloud(true);
        soundcloudApi.requests.clear();

        services.library()->syncLikes({QVariantMap{
            {"id", "31"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Неошансон"}, {"durationMs", 187000}}});
        QVERIFY(soundcloudApi.waitFor("/users/555/track_likes/88", 1, 10000));
        HttpRequest like = soundcloudApi.requestsTo("/users/555/track_likes/88")[0];
        QCOMPARE(like.method, QByteArray("PUT"));
        QCOMPARE(like.headers["authorization"], QByteArray("OAuth 2-oauth"));
        QTRY_COMPARE(services.library()->mirrored("SoundCloud").size(), 1);

        services.library()->syncLikes({});
        QVERIFY(soundcloudApi.waitFor("/users/555/track_likes/88", 2, 10000));
        QCOMPARE(soundcloudApi.requestsTo("/users/555/track_likes/88")[1].method, QByteArray("DELETE"));
        QTRY_VERIFY(services.library()->mirrored("SoundCloud").isEmpty());
    }

    // понравившееся в YouTube Music через cookies браузера: подпись в порядке,
    // в MAIN только добавить
    void youTubeLikesAreImported() {
        QTemporaryDir dir;
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        tools.add("yt-dlp", "echo \"$@\" >> \"$YTDLP_LOG\"\n"
                            "case \"$*\" in\n"
                            "  *list=LM*)\n"
                            "    echo '{\"id\": \"v1\", \"title\": \"Zamay - Неошансон (Official Video)\", "
                            "\"channel\": \"Zamay\", \"duration\": 187}' ;;\n"
                            "esac\n");

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy imported(&services, &ServiceManager::likesImported);
        QSignalSpy notices(&services, &ServiceManager::notice);

        services.importYouTubeLikes("chrome");
        QVERIFY(imported.wait());
        QCOMPARE(imported[0][0].toString(), QString("YouTube"));
        QVariantMap t = imported[0][1].toList()[0].toMap();
        QCOMPARE(t["artist"].toString(), QString("Zamay"));
        QCOMPARE(t["title"].toString(), QString("Неошансон"));
        QCOMPARE(t["service"].toString(), QString("YouTube"));

        QFile log(dir.filePath("yt-dlp.log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QString args = QString::fromUtf8(log.readAll());
        QVERIFY(args.contains("--cookies-from-browser chrome"));
        QVERIFY(args.contains("https://music.youtube.com/playlist?list=LM"));
        QVERIFY(!notices.isEmpty());
    }

    // доска Pinterest загрузилась: плейлистам раздаются новые обложки
    void pinterestBoardGivesCovers() {
        qputenv("MORPH_PINTEREST_API", yandexApi.url().toUtf8());
        yandexApi.on("GET", "/boards/1001/pins", [](const HttpRequest&) {
            QJsonObject image{{"1200x", QJsonObject{{"url", "https://i.pinimg.example/1.jpg"}}}};
            return jsonBody({{"items", QJsonArray{QJsonObject{{"media", QJsonObject{{"images", image}}}}}}});
        });
        QFile::remove(PathProvider::getDataPath() + "/pinterest.json");

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QSignalSpy ready(&services, &ServiceManager::coversReady);
        QSignalSpy cards(&services, &ServiceManager::dailyMixesReady);
        QVERIFY(services.newCover("playlist:Road").isEmpty());

        services.setPinterest("tok", "1001");
        QVERIFY(ready.wait());
        QVERIFY(services.hasCovers());
        QCOMPARE(cards.last()[0].toString(), QString("morph"));
        QString cover = services.newCover("playlist:Road");
        QCOMPARE(cover, QString("https://i.pinimg.example/1.jpg"));
        QVERIFY(services.isPinterestCover(cover));
        qunsetenv("MORPH_PINTEREST_API");
    }

    // суточный токен истёк: в SoundCloud ничего не пишется
    void expiredTokenWritesNothing() {
        QFile::remove(PathProvider::getDataPath() + "/library.json");
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        QByteArray claims =
            QJsonDocument(QJsonObject{{"client_id", "web"}, {"exp", 1000}}).toJson(QJsonDocument::Compact);
        services.setSoundCloudClientId("eyJhbGciOiJSUzI1NiJ9." + claims.toBase64(QByteArray::Base64UrlEncoding) +
                                       ".sig");
        services.setSyncToSoundCloud(true);
        soundcloudApi.requests.clear();

        services.library()->syncLikes(
            {QVariantMap{{"id", "32"}, {"service", "Yandex"}, {"artist", "A"}, {"title", "B"}}});
        QTest::qWait(1800);
        QVERIFY(soundcloudApi.requestsTo("/me").isEmpty());
        QVERIFY(soundcloudApi.requestsTo("/search/tracks").isEmpty());
    }

    // без синхронизации связь ищется, но коллекция в Яндексе не меняется
    void linkingWithoutSyncLeavesYandexAlone() {
        QFile::remove(PathProvider::getDataPath() + "/library.json");
        serveZamaySearch();
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setYandexToken("f");
        yandexApi.requests.clear();

        QVariantMap liked{{"id", "s2"}, {"service", "SoundCloud"}, {"artist", "Zamay"}, {"title", "Неошансон"}};
        services.library()->syncLikes({liked});
        QVERIFY(yandexApi.waitFor("/search"));
        QTRY_COMPARE(services.library()->sourcesOf("s2").size(), 2);
        services.setLiked(liked, true);
        QTest::qWait(200);
        QVERIFY(yandexApi.requestsTo("/users/5/likes/tracks/add-multiple").isEmpty());
    }

    // лайк 2 убран из каталога: волна Яндекса его не вернёт, а смешанная вернёт
    void removedLikeComesBackInTheWave() {
        auto track = [](int id, bool available = true) {
            return QJsonObject{{"id", id},
                               {"title", "Song " + QString::number(id)},
                               {"available", available},
                               {"artists", QJsonArray{QJsonObject{{"name", "Artist"}}}}};
        };
        yandexApi.on("GET", "/account/status",
                     [](const HttpRequest&) { return jsonResult(QJsonObject{{"account", QJsonObject{{"uid", 5}}}}); });
        yandexApi.on("GET", "/users/5/likes/tracks", [](const HttpRequest&) {
            QJsonArray liked{QJsonObject{{"id", "1"}, {"timestamp", "2026-02"}},
                             QJsonObject{{"id", "2"}, {"timestamp", "2026-01"}}};
            return jsonResult(QJsonObject{{"library", QJsonObject{{"tracks", liked}}}});
        });
        yandexApi.on("POST", "/tracks",
                     [track](const HttpRequest&) { return jsonResult(QJsonArray{track(1), track(2, false)}); });
        yandexApi.on("POST", "/rotor/session/new", [track](const HttpRequest&) {
            QJsonArray sequence;
            for (int id = 101; id <= 110; ++id) sequence.append(QJsonObject{{"track", track(id)}});
            return jsonResult(QJsonObject{{"radioSessionId", "s1"}, {"batchId", "b1"}, {"sequence", sequence}});
        });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        // тут проверяется миксер, порядок своей модели не нужен
        services.setSmartWaves(false);
        QSignalSpy likes(&services, &ServiceManager::likesFetched);
        QSignalSpy wave(&services, &ServiceManager::waveReady);
        services.setYandexToken("d");
        QVERIFY(likes.wait());

        services.getWave();
        QDeadlineTimer deadline(5000);
        while (!deadline.hasExpired() && (wave.isEmpty() || wave.last()[0].toString() != "Yandex")) wave.wait(100);
        QCOMPARE(wave.last()[0].toString(), QString("Yandex"));

        QStringList ids;
        for (const QVariant& t : wave.last()[1].toList()) ids.append(t.toMap()["id"].toString());
        QCOMPARE(ids.size(), 11);
        QCOMPARE(ids.last(), QString("2"));
        QVERIFY(!ids.contains("1"));

        // миксер тем временем ищет похожее через yt-dlp, процессу надо дать закончиться
        QTest::qWait(300);
    }
};

QTEST_GUILESS_MAIN(TestServiceManager)
#include "tst_service_manager.moc"
