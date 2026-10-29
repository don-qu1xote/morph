#include <QTest>

#include "core/TrackMatch.h"

// На реальных подписях с YouTube и SoundCloud: какая версия считается той же песней
class TestTrackMatch : public QObject {
    Q_OBJECT

    static QVariantMap track(const QString& artist, const QString& title, qint64 durationMs = 0) {
        return {{"artist", artist}, {"title", title}, {"durationMs", durationMs}};
    }

    static double score(const QVariantMap& wanted, const QVariantMap& candidate) {
        return TrackMatch::score(wanted, candidate);
    }

private slots:
    void sameSongMatchesAcrossServices() {
        QCOMPARE(TrackMatch::key(track("Ежемесячные", "Молотилка")),
                 TrackMatch::key(track("Channel", "Ежемесячные - Молотилка (Official Video)")));
        QCOMPARE(TrackMatch::key(track("Lead, Guest", "Song")), TrackMatch::key(track("Lead", "Song (feat. Guest)")));
        QCOMPARE(TrackMatch::key(track("Ёлка", "Прованс")), TrackMatch::key(track("Елка", "Прованс")));
        QVERIFY(TrackMatch::key(track("One", "Intro")) != TrackMatch::key(track("Other", "Intro")));
    }

    void exactUploadIsConfident() {
        QVariantMap wanted = track("Zamay", "Неошансон", 187000);
        QVERIFY(score(wanted, track("Zamay - Topic", "Неошансон", 187000)) >= 0.85);
        QVERIFY(score(wanted, track("ZAMAY", "Zamay - Неошансон [Official Audio]", 188000)) >= 0.85);
    }

    void liveCoverAndRemixLoseToTheOriginal() {
        QVariantMap wanted = track("Artist", "Song", 200000);
        double original = score(wanted, track("Artist", "Artist - Song", 201000));
        QVERIFY(score(wanted, track("Artist", "Artist - Song (Live at Arena)", 245000)) < 0.5);
        QVERIFY(score(wanted, track("Fan", "Song cover", 200000)) < original);
        QVERIFY(score(wanted, track("Artist", "Song (Slowed + Reverb)", 260000)) < 0.5);
        // но если ищут как раз лайв, штрафа нет
        QVERIFY(score(track("Artist", "Song (Live)", 245000), track("Artist", "Artist - Song (Live)", 245000)) >= 0.85);
    }

    void otherSongOfTheSameArtistIsRejected() {
        QVariantMap wanted = track("Artist", "Город", 180000);
        QVERIFY(score(wanted, track("Artist", "Artist - Город огней", 240000)) < 0.5);
        QVERIFY(score(wanted, track("Artist", "Artist - Ночь", 180000)) < 0.5);
    }

    // латиница вместо кириллицы в имени артиста: спасают название и длительность
    void transliteratedArtistStillPasses() {
        QVariantMap wanted = track("Скриптонит", "Космос", 213000);
        double s = score(wanted, track("Skryptonite", "Космос", 213000));
        QVERIFY(s >= 0.5);
        QVERIFY(s < 0.85);
    }

    void unknownDurationIsNeutral() {
        QVariantMap wanted = track("Zamay", "Неошансон");
        QVERIFY(score(wanted, track("Zamay", "Неошансон", 187000)) >= 0.85);
    }

    void queryRoundTrip() {
        QVariantMap parsed = TrackMatch::fromQuery("  Валентин Дядька - Лондон против всех - Live ");
        QCOMPARE(parsed["artist"].toString(), QString("Валентин Дядька"));
        QCOMPARE(parsed["title"].toString(), QString("Лондон против всех - Live"));
        QCOMPARE(TrackMatch::fromQuery("Title Only")["artist"].toString(), QString());
        QCOMPARE(TrackMatch::query(parsed), QString("Валентин Дядька - Лондон против всех - Live"));
        QCOMPARE(TrackMatch::query(TrackMatch::fromQuery("Title Only")), QString("Title Only"));
    }
};

QTEST_GUILESS_MAIN(TestTrackMatch)
#include "tst_track_match.moc"
