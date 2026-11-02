#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

#include "core/Recommender.h"
#include "core/TrackMatch.h"

// История и профиль вкуса: из чего потом ранжируются волны и собираются плейлисты
class TestRecommender : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    static constexpr qint64 kDay = 24 * 3600;
    static constexpr qint64 kStart = 1'800'000'000;

    static QVariantMap track(const QString& id, const QString& artist, const QString& title = "Song") {
        return {{"id", id}, {"service", "Yandex"}, {"artist", artist}, {"title", title}, {"durationMs", 200000}};
    }

    static QString artist(const QString& name) { return TrackMatch::artistKey(track("x", name)); }

    QString historyPath() const { return dir.filePath("listening.jsonl"); }

private slots:
    void init() {
        QFile::remove(historyPath());
        QFile::remove(dir.filePath("ranker.json"));
    }

    static QStringList artists(const QVariantList& tracks) {
        QStringList out;
        for (const QVariant& t : tracks) out.append(t.toMap()["artist"].toString());
        return out;
    }

    void finishedAndSkippedAreTold() {
        ListenEvent e;
        e.length = 200;
        e.played = 170;
        QVERIFY(e.finished());
        e.played = 12;
        QVERIFY(e.skipped());
        e.played = 90;
        QVERIFY(!e.finished() && !e.skipped());
        e.completed = true;
        QVERIFY(e.finished());
        e.kind = ListenEvent::Like;
        QVERIFY(!e.finished() && !e.skipped());
    }

    void historySurvivesRestartAndBrokenLine() {
        {
            Recommender rec(dir.path());
            rec.setClock([] { return kStart; });
            rec.listened(track("1", "Zamay"), 200, 200, true);
            rec.liked(track("1", "Zamay"), true);
            // QML не знал длину: берётся из трека
            rec.listened(track("2", "Zamay"), 20, 0, false);
        }
        QFile file(historyPath());
        QVERIFY(file.open(QIODevice::Append));
        file.write("{\"k\":\"listen\",\"at\":");
        file.close();

        Recommender rec(dir.path());
        const QList<ListenEvent>& events = rec.history().events();
        QCOMPARE(events.size(), 3);
        QCOMPARE(events[1].kind, ListenEvent::Like);
        QCOMPARE(events[2].length, 200.0);
        QVERIFY(events[2].skipped());
        QCOMPARE(events[0].at, kStart);
        QCOMPARE(events[0].track["title"].toString(), QString("Song"));
        QCOMPARE(rec.profile().heard(artist("Zamay")), 2);
    }

    void oldEventsAreCut() {
        {
            ListeningHistory history(historyPath());
            ListenEvent e;
            e.track = track("1", "Many");
            for (int i = 0; i < 12001; ++i) {
                e.at = i;
                history.append(e);
            }
            QCOMPARE(history.events().size(), 10000);
            QCOMPARE(history.events().first().at, qint64(2001));
        }
        ListeningHistory reloaded(historyPath());
        QCOMPARE(reloaded.events().size(), 10000);
        QCOMPARE(reloaded.events().last().at, qint64(12000));
    }

    void songStatsCountPlaysAndSkips() {
        Recommender rec(dir.path());
        qint64 now = kStart;
        rec.setClock([&now] { return now; });
        rec.listened(track("1", "A", "Hit"), 200, 200, true);
        now += kDay;
        rec.listened(track("1", "A", "Hit"), 10, 200, false);
        now += kDay;
        rec.listened(track("1", "A", "Hit"), 200, 200, true);

        auto songs = rec.history().songs();
        ListeningHistory::SongStats hit = songs.value(TrackMatch::songKey(track("1", "A", "Hit")));
        QCOMPARE(hit.plays, 3);
        QCOMPARE(hit.finishes, 2);
        QCOMPARE(hit.skips, 1);
        QCOMPARE(hit.lastPlayed, now);
        QCOMPARE(rec.history().songs(now).value(TrackMatch::songKey(track("1", "A", "Hit"))).plays, 1);
    }

    void recentIsNewestFirstWithoutSkips() {
        Recommender rec(dir.path());
        qint64 now = kStart;
        rec.setClock([&now] { return now; });
        rec.listened(track("1", "A", "First"), 200, 200, true);
        now += 60;
        rec.listened(track("2", "B", "Second"), 120, 200, false);
        rec.listened(track("3", "C", "Skipped"), 5, 200, false);
        rec.liked(track("4", "D", "Only liked"), true);
        // та же песня с другой площадки: в ряду одна, самая свежая
        now += 60;
        QVariantMap again = track("sc1", "A", "First");
        again["service"] = "SoundCloud";
        rec.listened(again, 200, 200, true);

        QVariantList recent = rec.history().recent(10);
        QCOMPARE(recent.size(), 2);
        QCOMPARE(recent[0].toMap()["id"].toString(), QString("sc1"));
        QCOMPARE(recent[1].toMap()["id"].toString(), QString("2"));
        QCOMPARE(recent[0].toMap()["playedAt"].toLongLong(), now);
        QCOMPARE(rec.history().recent(1).size(), 1);
    }

    // лайки дают вкус надолго, скипы тянут вниз, недавнее затухает
    void affinityFollowsListening() {
        Recommender rec(dir.path());
        qint64 now = kStart;
        rec.setClock([&now] { return now; });
        rec.setLikes({track("1", "Loved"), track("2", "Loved", "Other"), track("3", "Once")});

        for (int i = 0; i < 5; ++i) rec.listened(track("9", "Skipped"), 5, 200, false);
        for (int i = 0; i < 6; ++i) rec.listened(track("8", "Fresh"), 200, 200, true);

        const TasteProfile& p = rec.profile();
        QVERIFY(p.affinity(artist("Loved"), now) > p.affinity(artist("Once"), now));
        QVERIFY(p.affinity(artist("Once"), now) > 0);
        QVERIFY(p.affinity(artist("Skipped"), now) < -0.5);
        QVERIFY(!p.known(artist("Skipped")));
        QVERIFY(p.known(artist("Fresh")));
        QCOMPARE(p.affinity(artist("Nobody"), now), 0.0);

        double fresh = p.affinity(artist("Fresh"), now);
        double month = p.affinity(artist("Fresh"), now + 30 * kDay);
        QVERIFY(month < fresh);
        QVERIFY(month > 0);
        // лайки не затухают
        QCOMPARE(p.affinity(artist("Loved"), now + 365 * kDay), p.affinity(artist("Loved"), now));

        auto top = p.top(2, now);
        QCOMPARE(top.size(), 2);
        QCOMPARE(top[0].first, artist("Fresh"));
        QCOMPARE(p.name(top[1].first), QString("Loved"));
    }

    void rankerLearnsAndRemembers() {
        Ranker ranker;
        Ranker::Features skipped{{"bias", 1}, {"a:skipped", 1}};
        Ranker::Features loved{{"bias", 1}, {"a:loved", 1}};
        QCOMPARE(ranker.predict(skipped), ranker.predict(loved));
        for (int i = 0; i < 10; ++i) {
            ranker.learn(skipped, 0);
            ranker.learn(loved, 1);
        }
        QVERIFY(ranker.predict(skipped) < 0.3);
        QVERIFY(ranker.predict(loved) > 0.7);
        QCOMPARE(ranker.updates(), 20);

        Ranker restored;
        restored.load(ranker.save());
        QCOMPARE(restored.predict(loved), ranker.predict(loved));
        QVERIFY(restored.mean("a:skipped") < 0);
        // признак, которого модель не видела, начинает с подсказки
        QCOMPARE(restored.mean("affinity"), 2.0);
    }

    // чем больше примеров у признака, тем уже разброс и тем реже разведка
    void explorationNarrowsWithEvidence() {
        Ranker ranker;
        ranker.setSeed(7);
        Ranker::Features x{{"a:new", 1}};
        auto spread = [&ranker, &x]() {
            double sum = 0, sq = 0;
            for (int i = 0; i < 400; ++i) {
                double p = ranker.predict(x, true);
                sum += p;
                sq += p * p;
            }
            return std::sqrt(sq / 400 - (sum / 400) * (sum / 400));
        };
        double before = spread();
        for (int i = 0; i < 60; ++i) ranker.learn(x, i % 2);
        double after = spread();
        QVERIFY(before > 0.08);
        QVERIFY(after < before / 2);
    }

    // баланс 0.3: три незнакомых артиста на десять треков, на 4, 7 и 10 месте
    void rankKeepsTheShareOfNew() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        QVariantList liked, candidates;
        for (int i = 0; i < 10; ++i) {
            QVariantMap familiar = track("f" + QString::number(i), "Familiar " + QString::number(i));
            liked.append(familiar);
            candidates.append(familiar);
            candidates.append(track("n" + QString::number(i), "New " + QString::number(i)));
        }
        rec.setLikes(liked);

        QVariantList ranked = rec.rank(candidates, 10, false);
        QCOMPARE(ranked.size(), 10);
        QList<int> freshAt;
        for (int i = 0; i < ranked.size(); ++i) {
            if (rec.isNew(ranked[i].toMap())) freshAt.append(i);
        }
        QCOMPARE(freshAt, QList<int>({3, 6, 9}));

        rec.setNewShare(0.5);
        int fresh = 0;
        for (const QVariant& t : rec.rank(candidates, 10, false)) fresh += rec.isNew(t.toMap());
        QCOMPARE(fresh, 5);
    }

    // смешанная волна: советы SoundCloud и YouTube держат треть мест, даже
    // если модель пока ставит их ниже яндексовских
    void externalSourcesKeepTheirShare() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        QVariantList liked, candidates;
        for (int i = 0; i < 10; ++i) {
            QVariantMap own = track("y" + QString::number(i), "Liked " + QString::number(i));
            liked.append(own);
            candidates.append(own);
            QVariantMap outside = track("s" + QString::number(i), "Liked " + QString::number(i), "Other");
            outside["service"] = "SoundCloud";
            outside["via"] = "SoundCloud";
            candidates.append(outside);
        }
        rec.setLikes(liked);
        rec.setNewShare(0);

        QList<int> externalAt;
        QVariantList ranked = rec.rank(candidates, 9, false);
        for (int i = 0; i < ranked.size(); ++i) {
            if (Recommender::isExternal(ranked[i].toMap())) externalAt.append(i);
        }
        QVERIFY(externalAt.isEmpty() || externalAt.size() < 3);

        rec.setExternalShare(1.0 / 3);
        externalAt.clear();
        ranked = rec.rank(candidates, 9, false);
        for (int i = 0; i < ranked.size(); ++i) {
            if (Recommender::isExternal(ranked[i].toMap())) externalAt.append(i);
        }
        QCOMPARE(externalAt, QList<int>({2, 5, 8}));
        // кто посоветовал, модель видит отдельным признаком
        bool hasVia = false;
        for (const auto& f : rec.features(ranked[2].toMap())) hasVia = hasVia || f.first == "via:SoundCloud";
        QVERIFY(hasVia);
    }

    void sameArtistIsSpreadOut() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        QVariantList candidates;
        for (int a = 0; a < 6; ++a) {
            for (int t = 0; t < 3; ++t) {
                candidates.append(track(QString("%1-%2").arg(a).arg(t), "Artist " + QString::number(a),
                                        "Song " + QString::number(t)));
            }
        }
        // первый артист любимый: без правила он занял бы три первых места
        rec.setLikes({candidates[0], candidates[1], candidates[2]});

        QStringList order = artists(rec.rank(candidates, 10, false));
        QCOMPARE(order.first(), QString("Artist 0"));
        for (int i = 1; i < order.size(); ++i) QVERIFY(order[i] != order[i - 1]);
        for (const QString& a : std::as_const(order)) QVERIFY(order.count(a) <= 2);
    }

    void skipsPushAnArtistDown() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        for (int i = 0; i < 6; ++i) {
            rec.listened(track("x" + QString::number(i), "Skipped", "S" + QString::number(i)), 8, 200, false);
            rec.listened(track("y" + QString::number(i), "Kept", "K" + QString::number(i)), 200, 200, true);
        }
        QVariantList ranked = rec.rank({track("x9", "Skipped", "Next"), track("y9", "Kept", "Next")}, 2, false);
        QCOMPARE(artists(ranked), QStringList({"Kept", "Skipped"}));
        QVERIFY(rec.ranker().mean("a:" + artist("Skipped")) < rec.ranker().mean("a:" + artist("Kept")));
    }

    // четверть каждой партии ждёт в запасе, повторов нет, старое уходит
    void waveHoldsTheWorstBack() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        auto batch = [](int from) {
            QVariantList b;
            for (int i = from; i < from + 8; ++i) b.append(track(QString::number(i), "Artist " + QString::number(i)));
            return b;
        };

        QSet<QString> given;
        QVariantMap first;
        int total = 0;
        for (int turn = 0; turn < 5; ++turn) {
            QVariantList out = rec.nextWave(batch(turn * 8));
            QCOMPARE(out.size(), 6);
            if (first.isEmpty()) first = out[0].toMap();
            for (const QVariant& t : out) given.insert(t.toMap()["id"].toString());
            total += out.size();
        }
        QCOMPARE(given.size(), total);
        // та же песня в следующей партии в волну уже не вернётся
        for (const QVariant& t : rec.nextWave({first})) QVERIFY(t.toMap()["id"] != first["id"]);

        rec.resetWave();
        QCOMPARE(rec.nextWave(batch(100)).size(), 6);
    }

    void dislikeOutweighsAFewListens() {
        Recommender rec(dir.path());
        rec.setClock([] { return kStart; });
        rec.listened(track("1", "Meh"), 200, 200, true);
        rec.listened(track("2", "Meh"), 200, 200, true);
        rec.disliked(track("3", "Meh"));
        QVERIFY(rec.profile().affinity(artist("Meh"), kStart) < 0);
    }
};

QTEST_GUILESS_MAIN(TestRecommender)
#include "tst_recommender.moc"
