#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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
    void init() { QFile::remove(historyPath()); }

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
