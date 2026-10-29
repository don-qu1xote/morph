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

private slots:
    void initTestCase() { qputenv("MORPH_YANDEX_API", api.url().toUtf8()); }
    void init() { api.requests.clear(); }

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
};

QTEST_GUILESS_MAIN(TestYandexService)
#include "tst_yandex_service.moc"
