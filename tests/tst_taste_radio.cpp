#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "core/KnowledgeBase.h"
#include "core/MusicSpace.h"
#include "core/Recommender.h"
#include "core/TasteRadio.h"
#include "core/TrackMatch.h"
#include "core/WaveMixer.h"
#include "network/NetworkManager.h"
#include "services/LastFmApi.h"
#include "services/SoundCloudService.h"
#include "services/YouTubeService.h"
#include "support/FakeHttpServer.h"

// Свои кандидаты: соседи по Last.fm тех треков, что дослушал, ближе к вкусу
// выше, не больше двух на артиста, и в волне они встают в слоты миксера
class TestTasteRadio : public QObject {
    Q_OBJECT

    FakeHttpServer lastfm;
    QTemporaryDir dir;

    static QJsonObject similar(const QString& artist, const QString& title, double match) {
        return {{"name", title}, {"match", match}, {"duration", 200}, {"artist", QJsonObject{{"name", artist}}}};
    }

    void serve() {
        lastfm.on("GET", "/2.0/", [](const HttpRequest& r) {
            QString track = r.query.queryItemValue("track");
            QJsonArray tracks;
            if (track == "Неошансон") {
                tracks = {similar("Saluki", "Тейк", 0.9), similar("Saluki", "Детройт", 0.85),
                          similar("Saluki", "Lost", 0.8), similar("Portishead", "Roads", 0.7),
                          similar("Zamay", "Неошансон", 0.6)};
            } else if (track == "Ещё") {
                tracks = {similar("Saluki", "Тейк", 0.5), similar("Markul", "Серпантин", 0.4)};
            } else if (track == "Ritual") {
                tracks = {similar("Portishead", "Roads", 0.9), similar("Markul", "Серпантин", 0.6)};
            }
            return jsonBody({{"similartracks", QJsonObject{{"track", tracks}}}});
        });
    }

    static QVariantMap track(const QString& artist, const QString& title) {
        return {{"id", artist + title},
                {"service", "Yandex"},
                {"artist", artist},
                {"title", title},
                {"durationMs", 200000}};
    }

    // компания рэпа и компания трип-хопа, как в tst_music_space
    static std::shared_ptr<const MusicSpace> space() {
        QList<QPair<QString, QList<KnowledgeBase::Neighbor>>> g;
        QStringList a{"Zamay", "Saluki", "Pyrokinesis", "Markul"};
        QStringList b{"Portishead", "Massive Attack", "Tricky", "Björk"};
        for (const QStringList& crowd : {a, b}) {
            for (const QString& artist : crowd) {
                QList<KnowledgeBase::Neighbor> n;
                for (const QString& other : crowd) {
                    if (other != artist) n.append({other, {}, 0.8, 0});
                }
                g.append({artist, n});
            }
        }
        return std::make_shared<const MusicSpace>(MusicSpace::build(g));
    }

    static QVariantList wait(TasteRadio& radio, const QList<QVariantMap>& seeds, int count) {
        auto result = std::make_shared<QVariantList>();
        auto done = std::make_shared<bool>(false);
        radio.candidates(seeds, count, [result, done](const QVariantList& t) {
            *result = t;
            *done = true;
        });
        if (!QTest::qWaitFor([done] { return *done; }, 5000)) qWarning("no candidates");
        return *result;
    }

private slots:
    void initTestCase() { qputenv("MORPH_LASTFM_API", (lastfm.url() + "/2.0/").toUtf8()); }

    void init() {
        lastfm.reset();
        serve();
        QFile::remove(dir.filePath("knowledge.db"));
    }

    void noKeyNoCandidates() {
        NetworkManager net;
        LastFmApi api(&net);
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        Recommender rec(dir.path());
        TasteRadio radio(&base, &rec);
        QVERIFY(!radio.isAvailable());
        QVERIFY(wait(radio, {track("Zamay", "Неошансон")}, 5).isEmpty());
    }

    // соседи нескольких сидов вместе; сам сид обратно не приходит; Saluki не больше двух
    void neighborsOfSeedsComeBackPlayable() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        Recommender rec(dir.path());
        TasteRadio radio(&base, &rec);

        QVariantList found = wait(radio, {track("Zamay", "Неошансон"), track("Zamay", "Ещё")}, 10);
        QStringList titles;
        int saluki = 0;
        for (const QVariant& v : found) {
            QVariantMap t = v.toMap();
            titles.append(t["title"].toString());
            if (t["artist"] == "Saluki") ++saluki;
            QCOMPARE(t["via"].toString(), QString("Last.fm"));
            QCOMPARE(t["service"].toString(), QString("YouTube"));
            QCOMPARE(YouTubeService::queryFromTrackId(t["id"].toString()),
                     t["artist"].toString() + " - " + t["title"].toString());
        }
        QCOMPARE(saluki, 2);
        QVERIFY(!titles.contains("Неошансон"));
        QVERIFY(titles.contains("Серпантин"));
        // Тейк у обоих сидов: он первый
        QCOMPARE(titles.first(), QString("Тейк"));
    }

    // По Last.fm Portishead ближе, но вкус весь в рэпе: со вкусом первым
    // идёт Markul, без вкуса Portishead
    void tasteOutweighsPlainSimilarity() {
        QCOMPARE(TasteRadio::score(0.7, 1, true) > TasteRadio::score(0.7, -1, true), true);
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        Recommender rec(dir.path());
        auto s = space();
        rec.setLikes({track("Zamay", "1"), track("Pyrokinesis", "2")});
        rec.setVectors([s](const QVariantMap& t) { return s->song(t, {}); });
        TasteRadio radio(&base, &rec);
        radio.setSpace([s]() { return s; });

        QCOMPARE(wait(radio, {track("Unknown", "Ritual")}, 2).value(0).toMap()["artist"].toString(), QString("Markul"));

        Recommender plain(dir.path());
        TasteRadio blind(&base, &plain);
        QCOMPARE(wait(blind, {track("Unknown", "Ritual")}, 2).value(0).toMap()["artist"].toString(),
                 QString("Portishead"));
    }

    // в волне свои кандидаты встают в слоты миксера с via = Last.fm
    void waveGetsTasteSlots() {
        NetworkManager net;
        LastFmApi api(&net);
        api.setKey("key");
        KnowledgeBase base(&api, dir.filePath("knowledge.db"));
        Recommender rec(dir.path());
        TasteRadio radio(&base, &rec);
        SoundCloudService soundcloud(&net);
        YouTubeService youtube;
        WaveMixer mixer(&soundcloud, &youtube);
        mixer.setTasteRadio(&radio);
        QSignalSpy wave(&mixer, &WaveMixer::waveReady);

        mixer.addSeed(track("Zamay", "Неошансон"));
        QVariantList yandex;
        for (int i = 0; i < 6; ++i) yandex.append(track("Yandex Artist " + QString::number(i), "Song"));
        mixer.addBatch("Yandex", yandex);
        // сид и два трека партии: три вопроса к Last.fm с паузой между ними
        QVERIFY(lastfm.waitFor("/2.0/", 3));
        QTest::qWait(100);
        mixer.addBatch("Yandex", yandex);
        QCOMPARE(wave.count(), 2);
        int taste = 0;
        for (const QVariant& v : wave[1][1].toList()) {
            if (v.toMap()["via"] == "Last.fm") ++taste;
        }
        QVERIFY(taste >= 1);
    }
};

QTEST_GUILESS_MAIN(TestTasteRadio)
#include "tst_taste_radio.moc"
