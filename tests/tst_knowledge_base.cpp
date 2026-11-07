#include <QElapsedTimer>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "core/KnowledgeBase.h"
#include "network/NetworkManager.h"
#include "services/LastFmApi.h"
#include "support/FakeHttpServer.h"

// Last.fm и что от него остаётся в базе: кэш на месяц, переживает перезапуск,
// один запрос на одинаковые вопросы, не чаще четырёх в секунду, плохой ключ
class TestKnowledgeBase : public QObject {
    Q_OBJECT

    FakeHttpServer lastfm;
    QTemporaryDir dir;
    qint64 now = 1'800'000'000;

    QList<HttpRequest> calls(const QString& method) const {
        QList<HttpRequest> out;
        for (const HttpRequest& r : lastfm.requestsTo("/2.0/")) {
            if (r.query.queryItemValue("method") == method) out.append(r);
        }
        return out;
    }

    static QList<KnowledgeBase::Neighbor> wait(std::function<void(KnowledgeBase::Done)> ask) {
        auto result = std::make_shared<QList<KnowledgeBase::Neighbor>>();
        auto done = std::make_shared<bool>(false);
        ask([result, done](const QList<KnowledgeBase::Neighbor>& n) {
            *result = n;
            *done = true;
        });
        if (!QTest::qWaitFor([done] { return *done; }, 5000)) qWarning("no answer");
        return *result;
    }

    void serve() {
        lastfm.on("GET", "/2.0/", [](const HttpRequest& r) {
            QString method = r.query.queryItemValue("method");
            QString artist = r.query.queryItemValue("artist");
            if (r.query.queryItemValue("api_key") == "bad")
                return jsonBody({{"error", 10}, {"message", "Invalid API key"}});
            if (artist == "Nobody") return jsonBody({{"error", 6}, {"message", "Track not found"}});
            if (method == "track.getsimilar") {
                QJsonArray tracks{
                    QJsonObject{
                        {"name", "Тейк"}, {"match", 1}, {"duration", 180}, {"artist", QJsonObject{{"name", "Saluki"}}}},
                    QJsonObject{
                        {"name", "Плакса"}, {"match", "0.42"}, {"artist", QJsonObject{{"name", "Pyrokinesis"}}}}};
                return jsonBody({{"similartracks", QJsonObject{{"track", tracks}}}});
            }
            if (method == "artist.getsimilar") {
                // одиночный ответ Last.fm отдаёт объектом
                QJsonObject one{{"name", artist + " friend"}, {"match", "0.9"}};
                return jsonBody({{"similarartists", QJsonObject{{"artist", one}}}});
            }
            if (method == "artist.gettoptracks") {
                QJsonArray tracks{
                    QJsonObject{{"name", "Hit"}, {"playcount", "1000"}, {"artist", QJsonObject{{"name", artist}}}},
                    QJsonObject{{"name", "B-side"}, {"playcount", "250"}, {"artist", QJsonObject{{"name", artist}}}}};
                return jsonBody({{"toptracks", QJsonObject{{"track", tracks}}}});
            }
            return jsonBody({});
        });
    }

private slots:

    void initTestCase() { qputenv("MORPH_LASTFM_API", (lastfm.url() + "/2.0/").toUtf8()); }

    void init() {
        lastfm.reset();
        serve();
        QFile::remove(dir.filePath("knowledge.db"));
        now = 1'800'000'000;
    }

    void withoutKeyNothingIsAsked() {
        NetworkManager net;
        LastFmApi api(&net);
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        QVERIFY(!base.hasSource());
        QVERIFY(wait([&](auto done) { base.similarArtists("Zamay", done); }).isEmpty());
        QVERIFY(lastfm.requests.isEmpty());
    }

    void answersAreParsedCachedAndSurviveRestart() {
        QVariantMap track{{"artist", "Zamay"}, {"title", "Неошансон"}};
        {
            NetworkManager net;
            LastFmApi api(&net);
            api.setKey("key");
            KnowledgeBase base(&api, dir.filePath("knowledge.db"));
            base.setClock([this] { return now; });
            QSignalSpy learned(&base, &KnowledgeBase::learned);

            QList<KnowledgeBase::Neighbor> similar = wait([&](auto done) { base.similarTracks(track, done); });
            QCOMPARE(similar.size(), 2);
            QCOMPARE(similar[0].artist, QString("Saluki"));
            QCOMPARE(similar[0].title, QString("Тейк"));
            QCOMPARE(similar[0].durationMs, qint64(180000));
            QCOMPARE(similar[1].match, 0.42);
            QCOMPARE(learned.count(), 1);
            HttpRequest r = calls("track.getsimilar").value(0);
            QCOMPARE(r.query.queryItemValue("artist"), QString("Zamay"));
            QCOMPARE(r.query.queryItemValue("format"), QString("json"));

            QCOMPARE(wait([&](auto done) { base.similarTracks(track, done); }).size(), 2);
            QCOMPARE(calls("track.getsimilar").size(), 1);
        }
        // новый запуск: из SQLite, без сети
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        base.setClock([this] { return now; });
        QCOMPARE(base.knownSimilarTracks(track).size(), 2);
        QCOMPARE(wait([&](auto done) { base.similarTracks(track, done); }).size(), 2);
        QCOMPARE(calls("track.getsimilar").size(), 1);

        // через месяц ответ устарел и спрашивается снова
        now += 31LL * 24 * 3600;
        QCOMPARE(wait([&](auto done) { base.similarTracks(track, done); }).size(), 2);
        QCOMPARE(calls("track.getsimilar").size(), 2);
    }

    void sameQuestionGoesOutOnce() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        int answers = 0;
        for (int i = 0; i < 3; ++i)
            base.similarArtists("Zamay", [&answers](const auto& n) { answers += int(n.size()); });
        QTRY_COMPARE(answers, 3);
        QCOMPARE(calls("artist.getsimilar").size(), 1);
        QCOMPARE(base.knownSimilarArtists("ZAMAY")[0].artist, QString("Zamay friend"));
    }

    void topTracksAreShareOfTheBiggest() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        QList<KnowledgeBase::Neighbor> top = wait([&](auto done) { base.topTracks("Saluki", done); });
        QCOMPARE(top.size(), 2);
        QCOMPARE(top[0].match, 1.0);
        QCOMPARE(top[1].match, 0.25);
    }

    // нет такого трека: пустой ответ тоже запоминается, второй раз не спрашиваем
    void notFoundIsAnAnswer() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        QVariantMap track{{"artist", "Nobody"}, {"title", "Nothing"}};
        QVERIFY(wait([&](auto done) { base.similarTracks(track, done); }).isEmpty());
        QVERIFY(wait([&](auto done) { base.similarTracks(track, done); }).isEmpty());
        QCOMPARE(calls("track.getsimilar").size(), 1);
    }

    void badKeyStopsAsking() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("bad");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        QSignalSpy rejected(&api, &LastFmApi::keyRejected);
        QVERIFY(wait([&](auto done) { base.similarArtists("Zamay", done); }).isEmpty());
        QCOMPARE(rejected.count(), 1);
        QVERIFY(wait([&](auto done) { base.similarArtists("Saluki", done); }).isEmpty());
        QCOMPARE(calls("artist.getsimilar").size(), 1);

        // новый ключ: снова можно
        api.setKey("key");
        QCOMPARE(wait([&](auto done) { base.similarArtists("Saluki", done); }).size(), 1);
    }

    // Что пошло не так, видно снаружи: вид ключа, отказ, сбои подряд и текст
    // последнего. Без этого настройки час писали «Learning» при ключе в 31 символ
    void apiTellsWhatWentWrong() {
        QVERIFY(LastFmApi::looksLikeKey("0123456789abcdef0123456789ABCDEF"));
        QVERIFY(!LastFmApi::looksLikeKey("0123456789abcdef0123456789abcde"));
        QVERIFY(!LastFmApi::looksLikeKey("0123456789abcdef0123456789abcdez"));

        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        lastfm.reset();
        lastfm.on("GET", "/2.0/", [](const HttpRequest&) { return HttpResponse{503, "busy"}; });
        wait([&](auto done) { base.similarArtists("Zamay", done); });
        wait([&](auto done) { base.similarArtists("Saluki", done); });
        QCOMPARE(api.failuresInRow(), 2);
        QVERIFY(!api.lastError().isEmpty());
        QVERIFY(!api.isRejected());

        lastfm.reset();
        serve();
        wait([&](auto done) { base.similarArtists("Pyrokinesis", done); });
        QCOMPARE(api.failuresInRow(), 0);
        QVERIFY(api.lastError().isEmpty());

        api.setKey("bad");
        wait([&](auto done) { base.similarArtists("Markul", done); });
        QVERIFY(api.isRejected());
        QCOMPARE(api.failuresInRow(), 0);
    }

    // прогрев идёт по одному и с паузой: четыре запроса в секунду, не больше
    void warmGoesOneByOneAndSkipsKnown() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        wait([&](auto done) { base.similarArtists("Zamay", done); });
        lastfm.reset();
        serve();

        QElapsedTimer timer;
        timer.start();
        base.warm({"Zamay", "Saluki", "Pyrokinesis", "Markul"});
        QTRY_COMPARE_WITH_TIMEOUT(calls("artist.getsimilar").size(), 3, 5000);
        QVERIFY(timer.elapsed() >= 500);
        QStringList asked;
        for (const HttpRequest& r : calls("artist.getsimilar")) asked.append(r.query.queryItemValue("artist"));
        QCOMPARE(asked, QStringList({"Saluki", "Pyrokinesis", "Markul"}));
        QCOMPARE(base.allSimilarArtists().size(), 4);
    }
};

QTEST_GUILESS_MAIN(TestKnowledgeBase)
#include "tst_knowledge_base.moc"
