#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

#include "core/Library.h"
#include "core/MorphMixes.h"
#include "core/Recommender.h"
#include "core/TrackMatch.h"
#include "services/SoundCloudService.h"
#include "services/YandexService.h"
#include "support/FakeHttpServer.h"

// Плейлисты от morph: что попадает в каждый и откуда берётся новое
class TestMorphMixes : public QObject {
    Q_OBJECT

    static constexpr qint64 kDay = 24 * 3600;
    static constexpr qint64 kStart = 1'800'000'000;

    FakeHttpServer api;
    QTemporaryDir dir;

    static QVariantMap track(const QString& id, const QString& artist, const QString& title = "Song",
                             const QString& artistId = {}) {
        QVariantMap t{{"id", id},       {"service", "Yandex"},  {"artist", artist},
                      {"title", title}, {"durationMs", 200000}, {"coverUrl", "https://cover/" + id}};
        if (!artistId.isEmpty()) t["artistId"] = artistId;
        return t;
    }

    static QJsonObject yandexJson(int id, const QString& artist, const QString& title = "Song") {
        return {{"id", id},
                {"title", title},
                {"durationMs", 200000},
                {"artists", QJsonArray{QJsonObject{{"id", 900 + id}, {"name", artist}}}},
                {"albums", QJsonArray{QJsonObject{{"id", 1}, {"title", "Album"}}}}};
    }

    static QStringList ids(const QVariantList& tracks) {
        QStringList out;
        for (const QVariant& t : tracks) out.append(t.toMap()["id"].toString());
        return out;
    }

    static QStringList artists(const QVariantList& tracks) {
        QStringList out;
        for (const QVariant& t : tracks) out.append(t.toMap()["artist"].toString());
        return out;
    }

    struct World {
        NetworkManager net;
        YandexService yandex{&net};
        SoundCloudService soundcloud{&net};
        Recommender rec;
        Library library;
        MorphMixes mixes;
        qint64 now = kStart;

        explicit World(const QString& dir)
            : rec(dir), library(dir + "/library.json"), mixes(&rec, &library, &yandex, &soundcloud, dir) {
            yandex.setToken("token");
            rec.setClock([this] { return now; });
        }

        void likes(const QVariantList& tracks) {
            library.syncLikes(tracks);
            rec.setLikes(library.tracks());
        }

        QVariantList build(const QString& id) {
            QSignalSpy ready(&mixes, &MorphMixes::ready);
            mixes.build(id);
            if (ready.isEmpty() && !ready.wait()) return {};
            return ready[0][2].toList();
        }
    };

    // сессия: подряд дослушанные треки артистов, с промежутком в пять минут
    static void session(World& w, const QStringList& names) {
        for (const QString& name : names) {
            w.rec.listened(track(name + QString::number(w.now), name), 200, 200, true);
            w.now += 300;
        }
        w.now += 5 * 3600;
    }

private slots:
    void initTestCase() { qputenv("MORPH_YANDEX_API", api.url().toUtf8()); }

    void init() {
        api.reset();
        for (const QString& f : {"listening.jsonl", "ranker.json", "library.json", "discover.json"})
            QFile::remove(dir.filePath(f));
    }

    void onRepeatIsTheMostPlayed() {
        World w(dir.path());
        for (int i = 0; i < 3; ++i) w.rec.listened(track("a", "Often", "A"), 200, 200, true);
        for (int i = 0; i < 2; ++i) w.rec.listened(track("b", "Twice", "B"), 200, 200, true);
        w.rec.listened(track("c", "Once", "C"), 200, 200, true);
        for (int i = 0; i < 3; ++i) w.rec.listened(track("d", "Skipped", "D"), 5, 200, false);

        QCOMPARE(ids(w.build("morph:onrepeat")), QStringList({"a", "b"}));
        // месяц спустя это уже не «на повторе»
        w.now += 31 * kDay;
        QVERIFY(w.build("morph:onrepeat").isEmpty());
    }

    // кого слушаешь в одних и тех же сессиях, те и попадают в один микс
    void artistsHeardTogetherFormAGroup() {
        World w(dir.path());
        for (int day = 0; day < 4; ++day) {
            session(w, {"Rap 1", "Rap 2", "Rap 3", "Rap 4"});
            session(w, {"Rock 1", "Rock 2", "Rock 3"});
        }

        QList<QStringList> groups = w.mixes.artistGroups();
        QCOMPARE(groups.size(), 2);
        auto names = [&w](const QStringList& keys) {
            QStringList out;
            for (const QString& k : keys) out.append(w.rec.profile().name(k));
            std::sort(out.begin(), out.end());
            return out;
        };
        QCOMPARE(names(groups[0]), QStringList({"Rap 1", "Rap 2", "Rap 3", "Rap 4"}));
        QCOMPARE(names(groups[1]), QStringList({"Rock 1", "Rock 2", "Rock 3"}));

        QVariantList cards = w.mixes.cards();
        QCOMPARE(cards[0].toMap()["name"].toString(), QString("Daily Mix 1"));
        QVERIFY(cards[0].toMap()["subtitle"].toString().startsWith("Rap"));
        QCOMPARE(cards[0].toMap()["permalink_url"].toString(), QString("morph:daily1"));
        QCOMPARE(cards[0].toMap()["service"].toString(), QString("morph"));
    }

    // истории ещё нет: группы видны по тому, кого лайкал друг за другом
    void likesAloneGiveGroups() {
        World w(dir.path());
        QVariantList liked;
        for (const QString& phase : {"Old", "New"}) {
            for (int round = 0; round < 3; ++round) {
                for (int a = 1; a <= 4; ++a)
                    liked.append(track(QString("%1%2%3").arg(phase).arg(a).arg(round), phase + " " + QString::number(a),
                                       "T" + QString::number(round)));
            }
        }
        w.likes(liked);
        QList<QStringList> groups = w.mixes.artistGroups();
        QCOMPARE(groups.size(), 2);
        for (const QStringList& g : groups) {
            QString phase = w.rec.profile().name(g.first()).section(' ', 0, 0);
            for (const QString& k : g) QVERIFY(w.rec.profile().name(k).startsWith(phase));
        }
    }

    // в миксе своё и похожее новое, примерно треть новых
    void dailyMixAddsSimilarNewTracks() {
        World w(dir.path());
        QVariantList liked;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 3; ++j)
                liked.append(
                    track(QString("%1%2").arg(i).arg(j), "Own " + QString::number(i), "T" + QString::number(j)));
        }
        w.likes(liked);
        for (int day = 0; day < 3; ++day) session(w, {"Own 0", "Own 1", "Own 2", "Own 3"});
        api.on("GET", "/tracks/00/similar", [](const HttpRequest&) {
            QJsonArray similar;
            for (int i = 1; i <= 8; ++i) similar.append(yandexJson(500 + i, "Fresh " + QString::number(i)));
            return jsonResult(QJsonObject{{"similarTracks", similar}});
        });

        QVariantList mix = w.build("morph:daily1");
        QVERIFY(mix.size() >= 10);
        int fresh = artists(mix).filter("Fresh").size();
        QVERIFY(fresh >= 3);
        QVERIFY(fresh <= mix.size() / 2);
        QVERIFY(api.requestsTo("/tracks/00/similar").size() == 1);
    }

    // Discover Weekly: только незнакомые артисты, по треку на каждого, и до
    // понедельника тот же список
    void discoverIsNewArtistsOnceAWeek() {
        World w(dir.path());
        w.likes({track("1", "Known"), track("2", "Known Too"), track("3", "Known Three")});
        api.on("GET", "/tracks/1/similar", [](const HttpRequest&) {
            return jsonResult(QJsonObject{
                {"similarTracks", QJsonArray{yandexJson(11, "Known Too", "Other"), yandexJson(12, "Stranger"),
                                             yandexJson(13, "Stranger", "Second")}}});
        });
        api.on("GET", "/tracks/2/similar", [](const HttpRequest&) {
            return jsonResult(QJsonObject{{"similarTracks", QJsonArray{yandexJson(21, "Newcomer")}}});
        });

        QVariantList first = w.build("morph:discover");
        QStringList names = artists(first);
        std::sort(names.begin(), names.end());
        QCOMPARE(names, QStringList({"Newcomer", "Stranger"}));

        int asked = api.requestsTo("/tracks/1/similar").size();
        QCOMPARE(ids(w.build("morph:discover")), ids(first));
        QCOMPARE(api.requestsTo("/tracks/1/similar").size(), asked);
    }

    void releaseRadarTakesFreshAlbums() {
        World w(dir.path());
        w.likes({track("1", "Fav", "One", "77"), track("2", "Fav", "Two", "77"), track("3", "Other", "X", "88")});
        QString fresh = QDateTime::fromSecsSinceEpoch(kStart - 5 * kDay).toString(Qt::ISODate);
        QString old = QDateTime::fromSecsSinceEpoch(kStart - 400 * kDay).toString(Qt::ISODate);
        api.on("GET", "/artists/77/direct-albums", [fresh, old](const HttpRequest&) {
            QJsonArray albums{QJsonObject{{"id", 5}, {"title", "New"}, {"releaseDate", fresh}, {"coverUri", "c/%%"}},
                              QJsonObject{{"id", 6}, {"title", "Old"}, {"releaseDate", old}}};
            return jsonResult(QJsonObject{{"albums", albums}});
        });
        api.on("GET", "/albums/5/with-tracks", [](const HttpRequest&) {
            QJsonArray volume{yandexJson(51, "Fav", "Single A"), yandexJson(52, "Fav", "Single B")};
            return jsonResult(QJsonObject{{"volumes", QJsonArray{volume}}});
        });

        QSignalSpy ready(&w.mixes, &MorphMixes::ready);
        w.mixes.build("morph:releases");
        QVERIFY(ready.wait());
        QCOMPARE(ids(ready[0][2].toList()), QStringList({"51", "52"}));
        QCOMPARE(ready[0][1].toString(), QString("https://c/400x400"));
        QVERIFY(api.requestsTo("/albums/6/with-tracks").isEmpty());
    }

    // давно не звучавшие лайки, порядок держится всю неделю
    void rediscoverLeavesOutWhatYouPlay() {
        World w(dir.path());
        QVariantList liked;
        for (int i = 0; i < 40; ++i) liked.append(track(QString::number(i), "Artist " + QString::number(i)));
        w.likes(liked);
        for (int i = 0; i < 5; ++i) w.rec.listened(liked[i].toMap(), 200, 200, true);

        QVariantList list = w.build("morph:rediscover");
        QCOMPARE(list.size(), 30);
        for (int i = 0; i < 5; ++i) QVERIFY(!ids(list).contains(QString::number(i)));
        w.now += kDay;
        QCOMPARE(ids(w.build("morph:rediscover")), ids(list));
    }
};

QTEST_GUILESS_MAIN(TestMorphMixes)
#include "tst_morph_mixes.moc"
