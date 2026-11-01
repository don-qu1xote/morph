#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/Library.h"
#include "core/TrackMatch.h"

// MAIN: одна запись на песню, какие бы её версии ни были лайкнуты
class TestLibrary : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    static QVariantMap track(const QString& service, const QString& id, const QString& artist, const QString& title) {
        return {{"service", service}, {"id", id}, {"artist", artist}, {"title", title}, {"durationMs", 187000}};
    }

    // одна песня на Яндексе и SoundCloud, плюс ремикс, который песней не считается
    static QVariantList likes() {
        return {track("Yandex", "y1", "Zamay", "Неошансон"), track("SoundCloud", "s1", "uploader", "Zamay - Неошансон"),
                track("SoundCloud", "s2", "Zamay", "Неошансон (Remix)"), track("YouTube", "v1", "Other", "Song")};
    }

    static QStringList ids(const QVariantList& tracks) {
        QStringList out;
        for (const QVariant& t : tracks) out.append(t.toMap()["id"].toString());
        return out;
    }

    QString path() const { return dir.filePath("library.json"); }

private slots:
    void init() { QFile::remove(path()); }

    void versionsOfOneSongAreOneEntry() {
        Library library(path());
        QSignalSpy changed(&library, &Library::changed);
        library.syncLikes(likes());

        QCOMPARE(changed.count(), 1);
        QCOMPARE(library.count(), 3);
        QVariantList tracks = library.tracks();
        QCOMPARE(ids(tracks), QStringList({"y1", "s2", "v1"}));
        QCOMPARE(tracks[0].toMap()["alsoOn"].toString(), QString("SoundCloud"));
        QCOMPARE(tracks[0].toMap()["songKey"].toString(), TrackMatch::songKey(likes()[0].toMap()));
        QCOMPARE(library.sourcesOf("s1"), QStringList({"SoundCloud", "Yandex"}));
    }

    // убранный из Яндекса трек показывается версией, которая играет
    void unavailableVersionGivesWay() {
        Library library(path());
        library.setAvailability([](const QVariantMap& t) { return t["id"].toString() != "y1"; });
        library.syncLikes(likes());

        QVariantMap first = library.tracks()[0].toMap();
        QCOMPARE(first["id"].toString(), QString("s1"));
        QCOMPARE(first["alsoOn"].toString(), QString("Yandex"));
        QVERIFY(library.alternatives("s1").isEmpty());
        QCOMPARE(ids({library.alternatives("y1").value(0)}), QStringList({"s1"}));
    }

    void alternativesSkipTheOwnService() {
        Library library(path());
        library.syncLikes(likes());

        QList<QVariantMap> forSoundCloud = library.alternatives("s1");
        QCOMPARE(forSoundCloud.size(), 1);
        QCOMPARE(forSoundCloud[0]["id"].toString(), QString("y1"));
        QVERIFY(!forSoundCloud[0].contains("alsoOn"));
        QVERIFY(library.alternatives("v1").isEmpty());
        QVERIFY(library.alternatives("unknown").isEmpty());
    }

    // связь с Яндексом переживает перезапуск и снятый лайк
    void linksAreKept() {
        QString key = TrackMatch::songKey(track("YouTube", "v1", "Other", "Song"));
        {
            Library library(path());
            library.syncLikes(likes());
            // новые лайки первыми: ремикс лайкнут позже
            QCOMPARE(library.nextToLink(), TrackMatch::songKey(likes()[2].toMap()));
            library.link(key, track("Yandex", "y9", "Other", "Song"));
            QCOMPARE(library.alternatives("v1").value(0)["id"].toString(), QString("y9"));
        }

        Library library(path());
        library.syncLikes(likes());
        QCOMPARE(library.yandexId(key), QString("y9"));
        QCOMPARE(library.sourcesOf("v1"), QStringList({"YouTube", "Yandex"}));
        QCOMPARE(library.trackById("y9")["title"].toString(), QString("Song"));
        // у ремикса Яндекса нет, его очередь
        QCOMPARE(library.nextToLink(), TrackMatch::songKey(likes()[2].toMap()));

        library.syncLikes({});
        QCOMPARE(library.count(), 0);
        QCOMPARE(library.yandexId(key), QString("y9"));
    }

    // не нашлось: неделю эту песню не ищем
    void triedSongWaits() {
        Library library(path());
        library.syncLikes({track("SoundCloud", "s7", "Rare", "Track")});
        QString key = library.nextToLink();
        QVERIFY(!key.isEmpty());
        library.markLinkTried(key);
        QVERIFY(library.nextToLink().isEmpty());

        Library restarted(path());
        restarted.syncLikes({track("SoundCloud", "s7", "Rare", "Track")});
        QVERIFY(restarted.nextToLink().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "tst_library.moc"
