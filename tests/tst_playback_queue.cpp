#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include "core/PlaybackQueue.h"

class TestPlaybackQueue : public QObject {
    Q_OBJECT

    static QVariantMap track(const QString& id) { return {{"id", id}, {"service", "Yandex"}}; }

    static QStringList ids(const QVariantList& tracks) {
        QStringList out;
        for (const QVariant& t : tracks) out.append(t.toMap()["id"].toString());
        return out;
    }

private slots:
    void playNextGoesFirstAddGoesLast() {
        PlaybackQueue queue;
        QSignalSpy changed(&queue, &PlaybackQueue::tracksChanged);
        queue.add(track("a"));
        queue.add(track("b"));
        queue.playNext(track("c"));

        QCOMPARE(ids(queue.tracks()), QStringList({"c", "a", "b"}));
        QCOMPARE(queue.count(), 3);
        QCOMPARE(changed.size(), 3);
        QCOMPARE(queue.takeNext()["id"].toString(), QString("c"));
        QCOMPARE(ids(queue.tracks()), QStringList({"a", "b"}));
    }

    void playingFromTheMiddleDropsWhatWasBefore() {
        PlaybackQueue queue;
        for (const QString& id : {"a", "b", "c", "d"}) queue.add(track(id));

        QCOMPARE(queue.takeUpTo(2)["id"].toString(), QString("c"));
        QCOMPARE(ids(queue.tracks()), QStringList({"d"}));
        QVERIFY(queue.takeUpTo(5).isEmpty());
        queue.remove(0);
        QVERIFY(queue.takeNext().isEmpty());
    }

    void shuffleVisitsEveryTrackOncePerRound() {
        PlaybackQueue queue;
        queue.setSeed(7);
        queue.setShuffle(true);

        int current = 3;
        QSet<int> seen{current};
        for (int i = 0; i < 9; ++i) {
            current = queue.nextIndex(current, 10);
            QVERIFY(current >= 0 && current < 10);
            QVERIFY2(!seen.contains(current), "repeat inside one round");
            seen.insert(current);
        }
        QCOMPARE(seen.size(), 10);
        // круг пройден: дальше снова любой, кроме играющего
        QVERIFY(queue.nextIndex(current, 10) != current);
    }

    void previousWalksBackThroughWhatPlayed() {
        PlaybackQueue queue;
        queue.setSeed(1);
        queue.setShuffle(true);

        int first = 0;
        int second = queue.nextIndex(first, 5);
        int third = queue.nextIndex(second, 5);
        QCOMPARE(queue.previousIndex(third), second);
        QCOMPARE(queue.previousIndex(second), first);
        QCOMPARE(queue.previousIndex(first), -1);
    }

    void nothingToShuffleInATinyList() {
        PlaybackQueue queue;
        queue.setShuffle(true);
        QCOMPARE(queue.nextIndex(0, 1), -1);
        QCOMPARE(queue.nextIndex(0, 0), -1);
    }
};

QTEST_GUILESS_MAIN(TestPlaybackQueue)
#include "tst_playback_queue.moc"
