#include <QSignalSpy>
#include <QTest>

#include "core/ListenTracker.h"

class TestListenTracker : public QObject {
    Q_OBJECT

private slots:
    void skipCountsOnlyPlayingTime() {
        ListenTracker tracker;
        QSignalSpy finished(&tracker, &ListenTracker::listenFinished);

        tracker.trackStarted("Yandex", "A", "");
        tracker.durationChanged(180000);
        tracker.playbackChanged(true);
        QTest::qWait(200);
        tracker.playbackChanged(false);
        QTest::qWait(300);
        tracker.playbackChanged(true);
        QTest::qWait(100);

        // так QML переключает трек: play(url) останавливает старый и запускает новый,
        // и только потом зовёт reportPlay
        tracker.playbackChanged(false);
        tracker.durationChanged(0);
        tracker.playbackChanged(true);
        tracker.trackStarted("Yandex", "B", "");

        QCOMPARE(finished.count(), 1);
        QList<QVariant> skip = finished.takeFirst();
        QCOMPARE(skip[1].toString(), QString("A"));
        QVERIFY2(skip[3].toDouble() > 0.25 && skip[3].toDouble() < 0.45, qPrintable(skip[3].toString()));
        QCOMPARE(skip[4].toDouble(), 180.0);
        QCOMPARE(skip[5].toBool(), false);
    }

    void endOfTrackIsCompleted() {
        ListenTracker tracker;
        QSignalSpy finished(&tracker, &ListenTracker::listenFinished);

        tracker.playbackChanged(true);
        tracker.trackStarted("Yandex", "A", "");
        tracker.durationChanged(200000);
        QTest::qWait(150);
        tracker.trackEnded();
        tracker.playbackChanged(false);

        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished[0][4].toDouble(), 200.0);
        QCOMPARE(finished[0][5].toBool(), true);

        // повтор трека без нового reportPlay ничего не шлёт
        tracker.playbackChanged(true);
        tracker.playbackChanged(false);
        tracker.trackEnded();
        QCOMPARE(finished.count(), 1);
    }

    void nothingBeforeFirstTrack() {
        ListenTracker tracker;
        QSignalSpy finished(&tracker, &ListenTracker::listenFinished);

        tracker.playbackChanged(true);
        tracker.trackEnded();
        QCOMPARE(finished.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestListenTracker)
#include "tst_listen_tracker.moc"
