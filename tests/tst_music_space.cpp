#include <QTemporaryDir>
#include <QTest>

#include "core/MusicSpace.h"
#include "core/Recommender.h"
#include "core/TasteVectors.h"
#include "core/TrackMatch.h"

// Пространство музыки и вкус в нём: похожие артисты рядом, песня встаёт к
// своим похожим, долгий вкус и сессия двигаются так, как написано в формулах
class TestMusicSpace : public QObject {
    Q_OBJECT

    using Graph = QList<QPair<QString, QList<KnowledgeBase::Neighbor>>>;

    // две компании по пять артистов: внутри все похожи, между ними одна слабая связь
    static Graph twoCrowds() {
        Graph g;
        QStringList a{"Zamay", "Saluki", "Pyrokinesis", "Boulevard Depo", "Markul"};
        QStringList b{"Radiohead", "Portishead", "Massive Attack", "Björk", "Thom Yorke"};
        for (const QStringList& crowd : {a, b}) {
            for (const QString& artist : crowd) {
                QList<KnowledgeBase::Neighbor> n;
                for (const QString& other : crowd) {
                    if (other != artist) n.append({other, {}, 0.8, 0});
                }
                g.append({artist, n});
            }
        }
        g[0].second.append({"Radiohead", {}, 0.05, 0});
        return g;
    }

    static MusicSpace::Vector of(const MusicSpace& space, const QString& artist) {
        return *space.artist(TrackMatch::artistKey(QVariantMap{{"artist", artist}}));
    }

    static QVariantMap track(const QString& artist, const QString& title) {
        return {{"id", artist + title},
                {"service", "Yandex"},
                {"artist", artist},
                {"title", title},
                {"durationMs", 200000}};
    }

    static ListenEvent listen(const QVariantMap& t, qint64 at, bool finished) {
        ListenEvent e;
        e.track = t;
        e.at = at;
        e.length = 200;
        e.played = finished ? 200 : 10;
        e.completed = finished;
        return e;
    }

private slots:
    void similarArtistsStandTogether() {
        MusicSpace space = MusicSpace::build(twoCrowds());
        QCOMPARE(space.size(), 10);
        double inside = MusicSpace::cosine(of(space, "Zamay"), of(space, "Markul"));
        double across = MusicSpace::cosine(of(space, "Zamay"), of(space, "Portishead"));
        QVERIFY2(inside > 0.7, qPrintable(QString::number(inside)));
        QVERIFY2(across < 0.3, qPrintable(QString::number(across)));
        QCOMPARE(space.nearest(of(space, "Saluki"), 5).size(), 5);
        QStringList near;
        for (const auto& [key, cos] : space.nearest(of(space, "Saluki"), 5)) near.append(space.name(key));
        QVERIFY(near.contains("Pyrokinesis"));
        QVERIFY(!near.contains("Björk"));
    }

    void sameInputSamePoints() {
        MusicSpace one = MusicSpace::build(twoCrowds());
        MusicSpace two = MusicSpace::build(twoCrowds());
        QCOMPARE(MusicSpace::cosine(of(one, "Björk"), of(two, "Björk")), 1.0);
    }

    void tinyGraphGivesNoSpace() {
        Graph g{{"Zamay", {{"Saluki", {}, 0.9, 0}}}};
        QVERIFY(MusicSpace::build(g).isEmpty());
    }

    // артиста нет в графе, но его похожие треки у второй компании: туда он и встаёт
    void songFindsPlaceByItsSimilarTracks() {
        MusicSpace space = MusicSpace::build(twoCrowds());
        QList<KnowledgeBase::Neighbor> similar{{"Portishead", "Roads", 0.9, 0}, {"Massive Attack", "Teardrop", 0.7, 0}};
        std::optional<MusicSpace::Vector> v = space.song(track("Unknown Duo", "Night"), similar);
        QVERIFY(v.has_value());
        QVERIFY(MusicSpace::cosine(v.value_or(MusicSpace::Vector{}), of(space, "Radiohead")) >
                MusicSpace::cosine(v.value_or(MusicSpace::Vector{}), of(space, "Zamay")));
        QVERIFY(!space.song(track("Nobody", "Nothing"), {}).has_value());
    }

    void longTasteFollowsLikesAndForgetsSlowly() {
        MusicSpace space = MusicSpace::build(twoCrowds());
        auto vectorOf = [&space](const QVariantMap& t) { return space.song(t, {}); };
        TasteVectors taste;
        taste.rebuild({}, {track("Zamay", "1"), track("Saluki", "2")}, vectorOf);
        qint64 now = 1'800'000'000;
        QVERIFY(MusicSpace::cosine(taste.longTerm(now).value_or(MusicSpace::Vector{}), of(space, "Markul")) > 0.7);

        // старое забывается вдвое за 60 дней: свежий скип весит больше давних дослушиваний
        TasteVectors decayed;
        decayed.add(listen(track("Radiohead", "Creep"), now - 120 * 86400, true),
                    vectorOf(track("Radiohead", "Creep")));
        decayed.add(listen(track("Zamay", "3"), now, true), vectorOf(track("Zamay", "3")));
        QVERIFY(MusicSpace::cosine(decayed.longTerm(now).value_or(MusicSpace::Vector{}), of(space, "Zamay")) >
                MusicSpace::cosine(decayed.longTerm(now).value_or(MusicSpace::Vector{}), of(space, "Radiohead")));
    }

    // Сессия уходит туда, что слушаешь сейчас, и тем сильнее, чем дольше
    // слушаешь. Скип отталкивает, полчаса тишины начинают сессию заново
    void sessionPullsAndResets() {
        MusicSpace space = MusicSpace::build(twoCrowds());
        auto vectorOf = [&space](const QVariantMap& t) { return space.song(t, {}); };
        TasteVectors taste;
        taste.rebuild({}, {track("Zamay", "1"), track("Saluki", "2"), track("Markul", "3")}, vectorOf);
        qint64 now = 1'800'000'000;
        double before = MusicSpace::cosine(taste.current(now).value_or(MusicSpace::Vector{}), of(space, "Portishead"));

        QList<double> pull;
        for (int i = 0; i < 3; ++i) {
            QVariantMap t = track(i == 1 ? "Portishead" : "Massive Attack", QString::number(i));
            taste.add(listen(t, now + i * 200, true), vectorOf(t));
            pull.append(MusicSpace::cosine(taste.current(now + i * 200).value_or(MusicSpace::Vector{}),
                                           of(space, "Portishead")));
        }
        QVERIFY(pull[0] > before);
        QVERIFY(pull[2] > pull[0]);
        QCOMPARE(taste.sessionLength(now + 400), 3);

        taste.add(listen(track("Zamay", "4"), now + 600, false), vectorOf(track("Zamay", "4")));
        QVERIFY(MusicSpace::cosine(taste.session(now + 600).value_or(MusicSpace::Vector{}), of(space, "Zamay")) < 0);

        QVERIFY(!taste.session(now + 600 + 31 * 60).has_value());
        QCOMPARE(taste.sessionLength(now + 600 + 31 * 60), 0);
    }

    // в ранжировании это признаки cosU, cosS, cosL: близкое к вкусу выше
    void rankerPrefersWhatIsCloseToTaste() {
        QTemporaryDir dir;
        MusicSpace space = MusicSpace::build(twoCrowds());
        Recommender rec(dir.path());
        rec.setSeed(7);
        rec.setNewShare(0);
        rec.setLikes({track("Zamay", "1"), track("Saluki", "2"), track("Markul", "3")});
        rec.setVectors([&space](const QVariantMap& t) { return space.song(t, {}); });

        Ranker::Features close = rec.features(track("Pyrokinesis", "Легенда"));
        Ranker::Features far = rec.features(track("Björk", "Joga"));
        auto value = [](const Ranker::Features& x, const QString& name) {
            for (const auto& [n, v] : x) {
                if (n == name) return v;
            }
            return -2.0;
        };
        QVERIFY(value(close, "cosU") > 0.7);
        QVERIFY(value(far, "cosU") < 0.3);
        QCOMPARE(value(rec.features(track("Nobody", "Nothing")), "noVec"), 1.0);

        QVariantList ranked = rec.rank({track("Björk", "Joga"), track("Pyrokinesis", "Легенда")}, 2, false);
        QCOMPARE(ranked[0].toMap()["artist"].toString(), QString("Pyrokinesis"));
    }
};

QTEST_GUILESS_MAIN(TestMusicSpace)
#include "tst_music_space.moc"
