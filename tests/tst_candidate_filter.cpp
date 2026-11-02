#include <QDeadlineTimer>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/CandidateFilter.h"
#include "core/TrackMatch.h"
#include "services/YandexService.h"
#include "support/FakeHttpServer.h"

// Отбор кандидатов SoundCloud и YouTube: что выкидывается, как чинится
// подпись и когда трек меняется на версию из Яндекса
class TestCandidateFilter : public QObject {
    Q_OBJECT

    FakeHttpServer api;
    QTemporaryDir dir;

    static QVariantMap track(const QString& id, const QString& artist, const QString& title, int seconds = 187) {
        return {{"id", id},
                {"service", "SoundCloud"},
                {"artist", artist},
                {"title", title},
                {"durationMs", seconds * 1000}};
    }

    QString cachePath() const { return dir.filePath("candidates.json"); }

    // поиск в Яндексе находит «Zamay - Неошансон»; available решает тест
    void serveCatalog(bool available) {
        api.on("GET", "/search", [available](const HttpRequest&) {
            QJsonObject found{{"id", 31},
                              {"title", "Неошансон"},
                              {"durationMs", 187000},
                              {"available", available},
                              {"artists", QJsonArray{QJsonObject{{"id", 7}, {"name", "Zamay"}}}},
                              {"albums", QJsonArray{QJsonObject{{"id", 9}, {"title", "Album"}}}}};
            return jsonResult(QJsonObject{{"tracks", QJsonObject{{"results", QJsonArray{found}}}}});
        });
    }

    static QVariantList run(CandidateFilter& filter, const QVariantList& tracks) {
        QVariantList out;
        bool done = false;
        filter.process(tracks, "SoundCloud", [&](const QVariantList& clean) {
            out = clean;
            done = true;
        });
        QDeadlineTimer deadline(5000);
        while (!done && !deadline.hasExpired()) QTest::qWait(20);
        return out;
    }

private slots:
    void initTestCase() { qputenv("MORPH_YANDEX_API", api.url().toUtf8()); }

    void init() {
        api.reset();
        QFile::remove(cachePath());
    }

    void junkIsRejected() {
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Song (Remix)")).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Song slowed + reverb")).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "beats", "[FREE] Zamay Type Beat - Grey")).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Song (snippet)")).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Song", 30)).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Русский рэп 2 часа подряд", 7200)).isEmpty());
        QVERIFY(!CandidateFilter::rejectReason(track("1", "a", "Лучшее за год 1 hour", 600)).isEmpty());
        QVERIFY(CandidateFilter::rejectReason(track("1", "Michael Jackson", "Beat It", 258)).isEmpty());
        QVERIFY(CandidateFilter::rejectReason(track("1", "Zamay", "Неошансон")).isEmpty());
    }

    void captionIsCleaned() {
        QVariantMap c = TrackMatch::cleaned(track("1", "Channel", "Zamay - Неошансон (Official Video)"));
        QCOMPARE(c["artist"].toString(), QString("Zamay"));
        QCOMPARE(c["title"].toString(), QString("Неошансон"));
        c = TrackMatch::cleaned(track("1", "Lead", "Song (feat. Guest) [Премьера клипа]"));
        QCOMPARE(c["title"].toString(), QString("Song (feat. Guest)"));
        QCOMPARE(c["artist"].toString(), QString("Lead"));
    }

    // нашлось в Яндексе и отдаётся: дальше играет яндексовская версия, а
    // посоветовавший SoundCloud записан в via. Второй раз каталог не спрашивается
    void foundInYandexPlaysFromYandex() {
        serveCatalog(true);
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("t");
        CandidateFilter filter(&yandex, cachePath());

        QVariantList out = run(filter, {track("s1", "uploader", "Zamay - Неошансон (Official Audio)", 188),
                                        track("s2", "x", "Other (Remix)")});
        QCOMPARE(out.size(), 1);
        QVariantMap t = out[0].toMap();
        QCOMPARE(t["service"].toString(), QString("Yandex"));
        QCOMPARE(t["id"].toString(), QString("31"));
        QCOMPARE(t["via"].toString(), QString("SoundCloud"));
        QCOMPARE(t["artistId"].toString(), QString("7"));
        QCOMPARE(api.requestsTo("/search").size(), 1);
        QCOMPARE(api.requestsTo("/search")[0].query.queryItemValue("text"), QString("Zamay Неошансон"));

        // кэш на диске: новый фильтр тоже не спрашивает
        CandidateFilter restarted(&yandex, cachePath());
        QCOMPARE(run(restarted, {track("s3", "re-upload", "Zamay - Неошансон", 186)})[0].toMap()["id"].toString(),
                 QString("31"));
        QCOMPARE(api.requestsTo("/search").size(), 1);
    }

    // в Яндексе есть, но убран: играет свой трек, но с настоящими артистом и названием
    void removedFromYandexKeepsOwnTrack() {
        serveCatalog(false);
        NetworkManager net;
        YandexService yandex(&net);
        yandex.setToken("t");
        CandidateFilter filter(&yandex, cachePath());

        QVariantMap t = run(filter, {track("s1", "uploader", "zamay - неошансон", 187)})[0].toMap();
        QCOMPARE(t["id"].toString(), QString("s1"));
        QCOMPARE(t["service"].toString(), QString("SoundCloud"));
        QCOMPARE(t["artist"].toString(), QString("Zamay"));
        QCOMPARE(t["title"].toString(), QString("Неошансон"));
        QCOMPARE(t["via"].toString(), QString("SoundCloud"));
    }

    // без токена Яндекс не спрашиваем: иначе все треки записались бы в «нет в каталоге»
    void withoutTokenOnlyCleans() {
        NetworkManager net;
        YandexService yandex(&net);
        CandidateFilter filter(&yandex, cachePath());

        QVariantList out = run(filter, {track("v1", "Channel", "Artist - Song [Official Video]", 200)});
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].toMap()["artist"].toString(), QString("Artist"));
        QCOMPARE(out[0].toMap()["title"].toString(), QString("Song"));
        QVERIFY(api.requestsTo("/search").isEmpty());
        QVERIFY(!QFile::exists(cachePath()));
    }
};

QTEST_GUILESS_MAIN(TestCandidateFilter)
#include "tst_candidate_filter.moc"
