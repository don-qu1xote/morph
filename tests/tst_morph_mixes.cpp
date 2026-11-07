#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <memory>

#include "core/KnowledgeBase.h"
#include "core/Library.h"
#include "core/MorphMixes.h"
#include "core/MusicSpace.h"
#include "core/Recommender.h"
#include "core/TasteRadio.h"
#include "core/TrackMatch.h"
#include "services/LastFmApi.h"
#include "services/SoundCloudService.h"
#include "services/YandexService.h"
#include "support/FakeHttpServer.h"

// Плейлисты от morph: что попадает в каждый и откуда берётся новое
class TestMorphMixes : public QObject {
    Q_OBJECT

    static constexpr qint64 kDay = 24 * 3600;
    static constexpr qint64 kStart = 1'800'000'000;

    FakeHttpServer api;
    FakeHttpServer lastfmApi;
    QTemporaryDir dir;

    // компания рэпа и компания трип-хопа, внутри все похожи
    static std::shared_ptr<const MusicSpace> twoCrowds() {
        QList<QPair<QString, QList<KnowledgeBase::Neighbor>>> g;
        for (const QStringList& crowd : {QStringList{"Zamay", "Saluki", "Pyrokinesis", "Markul"},
                                         QStringList{"Portishead", "Massive Attack", "Tricky", "Björk"}}) {
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
        LastFmApi lastfm{&net};
        KnowledgeBase knowledge;
        TasteRadio radio;
        qint64 now = kStart;

        explicit World(const QString& dir)
            : rec(dir), library(dir + "/library.json"), mixes(&rec, &library, &yandex, &soundcloud, dir),
              knowledge(&lastfm, dir + "/knowledge.db"), radio(&knowledge, &rec) {
            yandex.setToken("token");
            rec.setClock([this] { return now; });
        }

        // своя модель вкуса включена: карта и Last.fm
        void taste(const std::shared_ptr<const MusicSpace>& space) {
            lastfm.setKey("key");
            rec.setVectors([space](const QVariantMap& t) { return space->song(t, {}); });
            radio.setSpace([space]() { return space; });
            mixes.setTaste([space]() { return space; }, &radio);
            mixes.setKnowledge(&knowledge);
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

    // ответ suggest, пойманный в тесте
    struct Suggested {
        QVariantList tracks;
        QString basis;
        bool done = false;
    };

    static MorphMixes::Suggestions into(Suggested& out) {
        return [&out](const QVariantList& tracks, const QString& basis) {
            out.tracks = tracks;
            out.basis = basis;
            out.done = true;
        };
    }

private slots:
    void initTestCase() {
        qputenv("MORPH_YANDEX_API", api.url().toUtf8());
        qputenv("MORPH_LASTFM_API", (lastfmApi.url() + "/2.0/").toUtf8());
    }

    void init() {
        api.reset();
        lastfmApi.reset();
        for (const QString& f : {"listening.jsonl", "ranker.json", "library.json", "discover.json", "knowledge.db"})
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

    // Лайки вперемешку и ни одной сессии: по истории рэп и трип-хоп не
    // разделить, а карта разводит их по разным миксам
    void mapSplitsWhatLikesMixed() {
        World w(dir.path());
        const QStringList order{"Zamay",       "Portishead", "Saluki", "Massive Attack",
                                "Pyrokinesis", "Tricky",     "Markul", "Björk"};
        QVariantList liked;
        for (int round = 0; round < 2; ++round) {
            for (const QString& a : order)
                liked.append(track(a + QString::number(round), a, "T" + QString::number(round)));
        }
        w.likes(liked);
        const QStringList rap{"Zamay", "Saluki", "Pyrokinesis", "Markul"};
        auto pure = [&](const QList<QStringList>& groups) {
            for (const QStringList& g : groups) {
                bool first = rap.contains(w.rec.profile().name(g.first()));
                for (const QString& k : g) {
                    if (rap.contains(w.rec.profile().name(k)) != first) return false;
                }
            }
            return !groups.isEmpty();
        };
        QVERIFY(!pure(w.mixes.artistGroups()));

        w.taste(twoCrowds());
        QList<QStringList> groups = w.mixes.artistGroups();
        QCOMPARE(groups.size(), 2);
        QVERIFY(pure(groups));
    }

    // Daily Mix рэпа: соседи по Last.fm из той же компании приходят, а трип-хоп
    // отсеивается, хоть Яндекс и Last.fm и считают его похожим
    void dailyMixKeepsToItsCenter() {
        World w(dir.path());
        QVariantList liked;
        for (int round = 0; round < 3; ++round) {
            for (const QString& a : {"Zamay", "Saluki", "Pyrokinesis"})
                liked.append(track(a + QString::number(round), a, "T" + QString::number(round)));
        }
        liked.append(track("p", "Portishead", "Glory Box"));
        liked.append(track("m", "Massive Attack", "Teardrop"));
        w.likes(liked);
        w.taste(twoCrowds());
        for (const QVariant& v : liked) {
            api.on("GET", "/tracks/" + v.toMap()["id"].toString() + "/similar", [](const HttpRequest&) {
                return jsonResult(QJsonObject{
                    {"similarTracks", QJsonArray{yandexJson(71, "Tricky", "Overcome"), yandexJson(72, "Fresh 1")}}});
            });
        }
        lastfmApi.on("GET", "/2.0/", [](const HttpRequest&) {
            QJsonArray tracks{
                QJsonObject{{"name", "Roads"}, {"match", 0.9}, {"artist", QJsonObject{{"name", "Portishead"}}}},
                QJsonObject{{"name", "Серпантин"}, {"match", 0.8}, {"artist", QJsonObject{{"name", "Markul"}}}}};
            return jsonBody({{"similartracks", QJsonObject{{"track", tracks}}}});
        });

        QStringList names = artists(w.build("morph:daily1"));
        QVERIFY2(names.contains("Markul"), qPrintable(names.join(", ")));
        QVERIFY(names.contains("Fresh 1"));
        QVERIFY(!names.contains("Tricky"));
        QVERIFY(!names.contains("Portishead"));
        QVERIFY(!lastfmApi.requestsTo("/2.0/").isEmpty());
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

    // Discover Weekly с карты: незнакомые артисты рядом с долгим вкусом и их
    // популярное по Last.fm. Знакомых и далёких от вкуса в ней нет
    void discoverFindsStrangersNearTaste() {
        World w(dir.path());
        w.likes({track("1", "Zamay"), track("2", "Saluki"), track("3", "Zamay", "Second")});
        w.taste(twoCrowds());
        lastfmApi.on("GET", "/2.0/", [](const HttpRequest& r) {
            QString artist = r.query.queryItemValue("artist");
            QJsonObject who{{"name", artist}};
            QJsonArray tracks{QJsonObject{{"name", artist + " Hit"}, {"playcount", "1000"}, {"artist", who}},
                              QJsonObject{{"name", artist + " B-side"}, {"playcount", "300"}, {"artist", who}}};
            return jsonBody({{"toptracks", QJsonObject{{"track", tracks}}}});
        });

        QVariantList list = w.build("morph:discover");
        QStringList names = artists(list);
        std::sort(names.begin(), names.end());
        QCOMPARE(names, QStringList({"Markul", "Pyrokinesis"}));
        for (const QVariant& v : list) QCOMPARE(v.toMap()["via"].toString(), QString("Last.fm"));
        // у каждого самое популярное, B-side второй в очереди
        for (const QVariant& v : list) QVERIFY(v.toMap()["title"].toString().endsWith(" Hit"));
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
            // не QJsonArray{volume}: clang видит в этом копию, а не массив из одного диска
            QJsonArray volumes;
            volumes.append(volume);
            return jsonResult(QJsonObject{{"volumes", volumes}});
        });

        QSignalSpy ready(&w.mixes, &MorphMixes::ready);
        w.mixes.build("morph:releases");
        QVERIFY(ready.wait());
        QCOMPARE(ids(ready[0][2].toList()), QStringList({"51", "52"}));
        QCOMPARE(ready[0][1].toString(), QString("https://c/400x400"));
        QVERIFY(api.requestsTo("/albums/6/with-tracks").isEmpty());
    }

    // пустой плейлист с названием: треки плейлистов Яндекса с таким названием и песни с ним
    void emptyPlaylistIsFilledByTitle() {
        World w(dir.path());
        api.on("GET", "/search", [](const HttpRequest& r) {
            if (r.query.queryItemValue("type") == "playlist") {
                QJsonObject found{{"owner", QJsonObject{{"uid", 5}}}, {"kind", 7}, {"title", "Night Drive"}};
                return jsonResult(QJsonObject{{"playlists", QJsonObject{{"results", QJsonArray{found}}}}});
            }
            return jsonResult(QJsonObject{
                {"tracks", QJsonObject{{"results", QJsonArray{yandexJson(61, "Searched", "Night Drive")}}}}});
        });
        api.on("GET", "/users/5/playlists/7", [](const HttpRequest&) {
            // третий трек без данных, как в больших плейлистах Яндекса
            QJsonArray tracks{QJsonObject{{"id", 71}, {"track", yandexJson(71, "Synth A")}},
                              QJsonObject{{"id", 72}, {"track", yandexJson(72, "Synth B")}}, QJsonObject{{"id", 73}}};
            return jsonResult(QJsonObject{{"title", "Night Drive"}, {"tracks", tracks}});
        });

        Suggested got;
        w.mixes.suggest("Night Drive", {}, 10, into(got));
        QTRY_VERIFY(got.done);
        QCOMPARE(got.basis, QString("title"));
        QStringList found = ids(got.tracks);
        std::sort(found.begin(), found.end());
        QCOMPARE(found, QStringList({"61", "71", "72"}));

        // у названия-заглушки искать нечего: подбор по вкусу, без поиска
        int searches = api.requestsTo("/search").size();
        Suggested placeholder;
        w.mixes.suggest("My Playlist #2", {}, 10, into(placeholder));
        QTRY_VERIFY(placeholder.done);
        QCOMPARE(placeholder.basis, QString("taste"));
        QCOMPARE(api.requestsTo("/search").size(), searches);
    }

    // в плейлисте есть треки: похожее на них, но без того, что там уже лежит
    void suggestionsSkipWhatIsAlreadyThere() {
        World w(dir.path());
        auto similar = [](const HttpRequest&) {
            QJsonArray tracks{yandexJson(2, "Fav", "Two"), yandexJson(81, "Newcomer", "Fresh"),
                              yandexJson(82, "Stranger", "Other")};
            return jsonResult(QJsonObject{{"similarTracks", tracks}});
        };
        api.on("GET", "/tracks/1/similar", similar);
        api.on("GET", "/tracks/2/similar", similar);

        QVariantList playlist{track("1", "Fav", "One"), track("2", "Fav", "Two")};
        Suggested got;
        w.mixes.suggest("Road", playlist, 10, into(got));
        QTRY_VERIFY(got.done);
        QCOMPARE(got.basis, QString("contents"));
        QStringList found = ids(got.tracks);
        std::sort(found.begin(), found.end());
        QCOMPARE(found, QStringList({"81", "82"}));
        QVERIFY(api.requestsTo("/search").isEmpty());

        Suggested two;
        w.mixes.suggest("Road", playlist, 1, into(two));
        QTRY_VERIFY(two.done);
        QCOMPARE(two.tracks.size(), 1);
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
