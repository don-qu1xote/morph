#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "services/YouTubeService.h"
#include "support/FakeTools.h"
#include "utils/PathProvider.h"

// Очередь вызовов yt-dlp за потоком: кто запускается, в каком порядке и сколько раз
class TestYouTubeService : public QObject {
    Q_OBJECT

    FakeTools tools;
    QTemporaryDir dir;

    // строки лога вида "start fast <цель>", "start default <цель>", "end <цель>"
    QStringList calls() const {
        QFile log(dir.filePath("yt-dlp.log"));
        if (!log.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
    }

    int indexOf(const QString& line) const { return calls().indexOf(line); }

    static QString watch(const QString& id) { return "https://www.youtube.com/watch?v=" + id; }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QDir().mkpath(PathProvider::getTrackCachePath());
        qputenv("YTDLP_LOG", dir.filePath("yt-dlp.log").toUtf8());
        // цель с "slow" ищется 0.4 с; с YTDLP_NO_FAST быстрый клиент ничего не находит
        tools.add("yt-dlp", "for last; do :; done\n"
                            "mode=default\n"
                            "case \"$*\" in *player_client=visionos*) mode=fast ;; esac\n"
                            "echo \"start $mode $last\" >> \"$YTDLP_LOG\"\n"
                            "case \"$last\" in *slow*) sleep 0.4 ;; esac\n"
                            "if [ $mode = fast ] && [ -n \"$YTDLP_NO_FAST\" ]; then\n"
                            "  echo \"end $last\" >> \"$YTDLP_LOG\"; exit 1\n"
                            "fi\n"
                            "echo \"end $last\" >> \"$YTDLP_LOG\"\n"
                            "echo '{\"url\": \"https://cdn.example/audio.m4a\", \"abr\": 129.5}'\n");
    }

    void init() {
        QFile::remove(dir.filePath("yt-dlp.log"));
        qunsetenv("YTDLP_NO_FAST");
    }

    void playUsesTheFastClient() {
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);
        QSignalSpy bitrates(&youtube, &BaseService::bitrateReady);

        youtube.resolveStreamUrl("abc");
        QVERIFY(streams.wait());

        QCOMPARE(calls().first(), "start fast " + watch("abc"));
        QCOMPARE(streams[0][1].toString(), QString("https://cdn.example/audio.m4a"));
        QCOMPARE(bitrates[0][1].toInt(), 130);
    }

    void fastMissRetriesWithDefaults() {
        qputenv("YTDLP_NO_FAST", "1");
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);
        QSignalSpy failures(&youtube, &YouTubeService::resolveFailed);

        youtube.resolveStreamUrl("abc");
        QVERIFY(streams.wait());

        QCOMPARE(streams.size(), 1);
        QCOMPARE(failures.size(), 0);
        QVERIFY(indexOf("start fast " + watch("abc")) < indexOf("start default " + watch("abc")));
    }

    void fastPathStopsAfterThreeMisses() {
        qputenv("YTDLP_NO_FAST", "1");
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);

        for (const QString& id : {"a1", "a2", "a3", "a4"}) {
            youtube.resolveStreamUrl(id);
            QVERIFY(streams.wait());
        }
        QVERIFY(calls().contains("start fast " + watch("a3")));
        QVERIFY(!calls().contains("start fast " + watch("a4")));
        QVERIFY(calls().contains("start default " + watch("a4")));
    }

    void sameTrackRunsYtDlpOnce() {
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);

        youtube.prefetch("slowone");
        youtube.resolveStreamUrl("slowone");
        youtube.resolveStreamUrl("slowone");
        QVERIFY(streams.wait());
        QTest::qWait(100);

        QCOMPARE(calls().filter("start ").size(), 1);
        QCOMPARE(streams.size(), 1);
    }

    void prefetchWaitsForThePlayingTrack() {
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);

        youtube.resolveStreamUrl("slowplay");
        youtube.prefetch("next");
        QTRY_COMPARE(streams.size(), 2);

        QCOMPARE(streams[0][0].toString(), QString("slowplay"));
        QVERIFY(indexOf("end " + watch("slowplay")) < indexOf("start fast " + watch("next")));
    }

    // переключились на трек, который ждал предзагрузки: он стартует сразу, не дожидаясь прошлого
    void playingAWaitingPrefetchStartsItAtOnce() {
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);

        youtube.resolveStreamUrl("slowfirst");
        youtube.prefetch("second");
        youtube.prefetch("third");
        youtube.resolveStreamUrl("third");
        QTRY_COMPARE(streams.size(), 3);

        QVERIFY(indexOf("start fast " + watch("third")) < indexOf("end " + watch("slowfirst")));
        QVERIFY(indexOf("end " + watch("slowfirst")) < indexOf("start fast " + watch("second")));
    }

    void queryTrackGoesThroughSearch() {
        YouTubeService youtube;
        QSignalSpy streams(&youtube, &BaseService::streamUrlReady);

        QString id = YouTubeService::queryTrackId("Artist - Song");
        youtube.prefetch(id);
        QVERIFY(streams.wait());
        QCOMPARE(streams[0][0].toString(), id);
        QCOMPARE(calls().first(), QString("start fast ytsearch1:Artist - Song"));
    }
};

QTEST_GUILESS_MAIN(TestYouTubeService)
#include "tst_youtube_service.moc"
