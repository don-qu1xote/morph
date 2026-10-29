#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QSignalSpy>
#include <QTest>

#include "services/LyricsService.h"
#include "support/FakeHttpServer.h"

class TestLyricsService : public QObject {
    Q_OBJECT

    FakeHttpServer yandexApi;
    FakeHttpServer lrclib;

    static QVariantMap track(const QString& id, const QString& service = "Yandex") {
        return {{"id", id},
                {"service", service},
                {"artist", "Zamay, Guest"},
                {"title", "Неошансон"},
                {"durationMs", 187400}};
    }

    static QStringList texts(const QVariantList& lines) {
        QStringList out;
        for (const QVariant& l : lines) out.append(l.toMap()["text"].toString());
        return out;
    }

private slots:
    void initTestCase() {
        qputenv("MORPH_YANDEX_API", yandexApi.url().toUtf8());
        qputenv("MORPH_LRCLIB_API", (lrclib.url() + "/api").toUtf8());
    }

    void init() {
        yandexApi.reset();
        lrclib.reset();
    }

    void parsesTimedLinesTagsAndChorusRepeats() {
        QVariantList lines = LyricsService::parseLrc("[ar:Zamay]\n[length: 03:07]\n[00:12.5] Первая\n"
                                                     "[00:20.00][01:10.00] Припев\n\n[00:31.505]Вторая\n[00:40.00]\n");
        QCOMPARE(texts(lines), QStringList({"Первая", "Припев", "Вторая", "", "Припев"}));
        QCOMPARE(lines[0].toMap()["timeMs"].toLongLong(), 12500);
        QCOMPARE(lines[2].toMap()["timeMs"].toLongLong(), 31505);
        QCOMPARE(lines[4].toMap()["timeMs"].toLongLong(), 70000);
    }

    void plainTextHasNoTimes() {
        QVariantList lines = LyricsService::parseLrc("Строка раз\nСтрока два\n");
        QCOMPARE(texts(lines), QStringList({"Строка раз", "Строка два"}));
        QCOMPARE(lines[0].toMap()["timeMs"].toLongLong(), -1);
    }

    // подпись: HMAC-SHA256 от id и времени, base64, «+» закодирован
    void yandexLyricsAreSignedAndSynced() {
        QString base = yandexApi.url();
        yandexApi.on("GET", "/tracks/101/lyrics", [base](const HttpRequest&) {
            return jsonResult(QJsonObject{{"downloadUrl", base + "/lrc/101"}});
        });
        yandexApi.on("GET", "/lrc/101",
                     [](const HttpRequest&) { return HttpResponse{200, "[00:01.00] Раз\n[00:02.00] Два\n"}; });

        NetworkManager net;
        LyricsService lyrics(&net);
        lyrics.setYandexToken("token");
        QSignalSpy ready(&lyrics, &LyricsService::lyricsReady);

        lyrics.fetch(track("101:555"));
        QVERIFY(ready.wait());
        QCOMPARE(ready[0][0].toString(), QString("101:555"));
        QCOMPARE(texts(ready[0][1].toList()), QStringList({"Раз", "Два"}));
        QVERIFY(ready[0][2].toBool());
        QCOMPARE(ready[0][3].toString(), QString("Yandex"));

        HttpRequest request = yandexApi.requestsTo("/tracks/101/lyrics").first();
        QString timestamp = request.query.queryItemValue("timeStamp");
        QByteArray expected = QMessageAuthenticationCode::hash(("101" + timestamp).toUtf8(), "p93jhgh689SBReK6ghtw62",
                                                               QCryptographicHash::Sha256)
                                  .toBase64();
        QCOMPARE(request.query.queryItemValue("sign", QUrl::FullyDecoded).toLatin1(), expected);
        QCOMPARE(request.headers["authorization"], QByteArray("OAuth token"));
        QCOMPARE(request.query.queryItemValue("format"), QString("LRC"));
    }

    void yandexSilentThenLrclib() {
        lrclib.on("GET", "/api/get", [](const HttpRequest&) {
            return jsonBody({{"syncedLyrics", "[00:05.00] Из LRCLIB"}, {"plainLyrics", "Из LRCLIB"}});
        });

        NetworkManager net;
        LyricsService lyrics(&net);
        lyrics.setYandexToken("token");
        QSignalSpy ready(&lyrics, &LyricsService::lyricsReady);

        lyrics.fetch(track("202"));
        QVERIFY(ready.wait());
        QCOMPARE(ready[0][3].toString(), QString("LRCLIB"));
        HttpRequest request = lrclib.requestsTo("/api/get").first();
        QCOMPARE(request.query.queryItemValue("artist_name"), QString("Zamay"));
        QCOMPARE(request.query.queryItemValue("track_name"), QString("Неошансон"));
        QCOMPARE(request.query.queryItemValue("duration"), QString("187"));
    }

    // точного совпадения нет: поиск, и у найденного только обычный текст
    void searchGivesPlainTextForForeignTracks() {
        lrclib.on("GET", "/api/search", [](const HttpRequest&) {
            QJsonObject found{{"plainLyrics", "Без меток\nВторая"}};
            return HttpResponse{200, QJsonDocument(QJsonArray{found}).toJson()};
        });

        NetworkManager net;
        LyricsService lyrics(&net);
        QSignalSpy ready(&lyrics, &LyricsService::lyricsReady);

        lyrics.fetch(track("yt1", "YouTube"));
        QVERIFY(ready.wait());
        QVERIFY(!ready[0][2].toBool());
        QCOMPARE(texts(ready[0][1].toList()), QStringList({"Без меток", "Вторая"}));
        QCOMPARE(lrclib.requestsTo("/api/search").first().query.queryItemValue("q"), QString("Zamay Неошансон"));
        QVERIFY(yandexApi.requests.isEmpty());

        // второй раз из памяти, без запросов
        lrclib.requests.clear();
        lyrics.fetch(track("yt1", "YouTube"));
        QCOMPARE(ready.size(), 2);
        QVERIFY(lrclib.requests.isEmpty());
    }

    void nothingAnywhere() {
        NetworkManager net;
        LyricsService lyrics(&net);
        QSignalSpy missing(&lyrics, &LyricsService::lyricsMissing);

        lyrics.fetch(track("sc1", "SoundCloud"));
        QVERIFY(missing.wait());
        QCOMPARE(missing[0][0].toString(), QString("sc1"));
    }
};

QTEST_GUILESS_MAIN(TestLyricsService)
#include "tst_lyrics_service.moc"
