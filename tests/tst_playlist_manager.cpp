#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "settings/PlaylistManager.h"

class TestPlaylistManager : public QObject {
    Q_OBJECT

private:
    static QVariantMap track(const QString& id, const QString& service = "Yandex") {
        return {{"id", id}, {"service", service}, {"title", "title " + id}};
    }

    static QStringList likedIds(PlaylistManager& pm) {
        QStringList ids;
        for (const QVariant& t : pm.getLikedTracks()) ids << t.toMap()["id"].toString();
        return ids;
    }

private slots:
    void toggleLikeAddsOnTopAndRemoves() {
        QJsonObject data;
        PlaylistManager pm(&data);
        QSignalSpy changed(&pm, &PlaylistManager::likesChanged);

        pm.toggleLike(track("1"));
        pm.toggleLike(track("2"));
        QCOMPARE(likedIds(pm), QStringList({"2", "1"}));
        QVERIFY(pm.isLiked("1"));

        pm.toggleLike(track("1"));
        QCOMPARE(likedIds(pm), QStringList({"2"}));
        QVERIFY(!pm.isLiked("1"));
        QCOMPARE(changed.count(), 3);
    }

    // в MAIN песня одна, поэтому снятый лайк снимается со всех её версий
    void unlikeRemovesEveryVersionOfTheSong() {
        QJsonObject data;
        PlaylistManager pm(&data);
        QVariantMap yandex{{"id", "y1"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Неошансон"}};
        QVariantMap soundcloud{
            {"id", "s1"}, {"service", "SoundCloud"}, {"artist", "uploader"}, {"title", "Zamay - Неошансон"}};
        QVariantMap remix{{"id", "s2"}, {"service", "SoundCloud"}, {"artist", "Zamay"}, {"title", "Неошансон (Remix)"}};
        pm.toggleLike(yandex);
        pm.toggleLike(soundcloud);
        pm.toggleLike(remix);

        QVariantList removed = pm.toggleLike(soundcloud);
        QCOMPARE(removed.size(), 2);
        QCOMPARE(likedIds(pm), QStringList({"s2"}));
        QVERIFY(pm.toggleLike(yandex).isEmpty());
    }

    // лайки YouTube только добавляются: старые в конец, новые наверх, без повторов песен
    void importedLikesAreOnlyAdded() {
        QJsonObject data;
        PlaylistManager pm(&data);
        pm.toggleLike({{"id", "y1"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Неошансон"}});

        QVariantList fromYouTube{
            QVariantMap{{"id", "v1"}, {"service", "YouTube"}, {"artist", "Zamay"}, {"title", "Неошансон"}},
            QVariantMap{{"id", "v2"}, {"service", "YouTube"}, {"artist", "Other"}, {"title", "Song"}}};
        QCOMPARE(pm.importLikes(fromYouTube, false), 1);
        QCOMPARE(likedIds(pm), QStringList({"y1", "v2"}));

        QVariantList newer{QVariantMap{{"id", "v3"}, {"service", "YouTube"}, {"artist", "New"}, {"title", "One"}},
                           fromYouTube[1]};
        QCOMPARE(pm.importLikes(newer, true), 1);
        QCOMPARE(likedIds(pm), QStringList({"v3", "y1", "v2"}));
        QCOMPARE(pm.importLikes(newer, true), 0);
    }

    void playlistKeepsNewestTrackFirst() {
        QJsonObject data;
        PlaylistManager pm(&data);

        pm.createPlaylist("road");
        pm.addToPlaylist("road", track("a"));
        pm.addToPlaylist("road", track("b"));
        pm.removeFromPlaylist("road", "a");

        QVariantList tracks = pm.getPlaylists()["road"].toMap()["tracks"].toList();
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks[0].toMap()["id"].toString(), QString("b"));
    }

    void renameKeepsTracks() {
        QJsonObject data;
        PlaylistManager pm(&data);

        pm.createPlaylistWithTracks("old", "", {track("x")});
        pm.renamePlaylist("old", "new", "cover");

        QVariantMap playlists = pm.getPlaylists();
        QVERIFY(!playlists.contains("old"));
        QCOMPARE(playlists["new"].toMap()["coverUrl"].toString(), QString("cover"));
        QCOMPARE(playlists["new"].toMap()["tracks"].toList().size(), 1);
    }
    // занятое имя не затирает чужой плейлист ни при создании, ни при переименовании
    void namesAreNeverTakenOver() {
        QJsonObject data;
        PlaylistManager pm(&data);
        QVERIFY(pm.createPlaylist("  Road  "));
        QVERIFY(pm.getPlaylists().contains("Road"));
        QVERIFY(!pm.createPlaylist("Road"));
        QVERIFY(!pm.createPlaylist("   "));
        pm.addToPlaylist("Road", track("1"));

        QVERIFY(pm.createPlaylist("Gym"));
        QVERIFY(!pm.renamePlaylist("Gym", "Road", ""));
        QCOMPARE(pm.getPlaylists()["Road"].toMap()["tracks"].toList().size(), 1);
        // своё же имя с новой обложкой можно
        QVERIFY(pm.renamePlaylist("Gym", "Gym", "https://cover"));
        QCOMPARE(pm.getPlaylists()["Gym"].toMap()["coverUrl"].toString(), QString("https://cover"));

        QCOMPARE(pm.uniqueName("Road"), QString("Road 2"));
        QCOMPARE(pm.uniqueName("Trip"), QString("Trip"));
        QCOMPARE(pm.defaultName(), QString("My Playlist #1"));
        QVERIFY(pm.createPlaylist(pm.defaultName()));
        QCOMPARE(pm.defaultName(), QString("My Playlist #2"));
    }

    // повторный импорт той же ссылки обновляет плейлист, чужой с тем же именем цел
    void savedListGetsFreeName() {
        QJsonObject data;
        PlaylistManager pm(&data);
        pm.createPlaylist("Daily Mix 1");
        pm.addToPlaylist("Daily Mix 1", track("mine"));

        QCOMPARE(pm.createPlaylistWithTracks("Daily Mix 1", "", {track("1")}), QString("Daily Mix 1 2"));
        QCOMPARE(pm.getPlaylists()["Daily Mix 1"].toMap()["tracks"].toList().size(), 1);

        QString url = "https://soundcloud.com/x/sets/y";
        QCOMPARE(pm.createPlaylistWithTracks("Set", "", {track("1")}, url), QString("Set"));
        QCOMPARE(pm.createPlaylistWithTracks("Set", "", {track("1"), track("2")}, url), QString("Set"));
        QCOMPARE(pm.getPlaylists()["Set"].toMap()["tracks"].toList().size(), 2);
        QCOMPARE(pm.createPlaylistWithTracks("Set", "", {track("3")}, "https://other"), QString("Set 2"));
    }

    // одна песня в плейлисте один раз, даже если добавить её с другой площадки
    void songIsAddedOnce() {
        QJsonObject data;
        PlaylistManager pm(&data);
        pm.createPlaylist("Road");
        pm.createPlaylist("Gym");
        QVariantMap yandex{{"id", "1"}, {"service", "Yandex"}, {"artist", "Zamay"}, {"title", "Song"}};
        QVariantMap soundcloud{{"id", "sc1"}, {"service", "SoundCloud"}, {"artist", "Zamay"}, {"title", "Song"}};

        QVERIFY(pm.addToPlaylist("Road", yandex));
        QVERIFY(!pm.addToPlaylist("Road", yandex));
        QVERIFY(!pm.addToPlaylist("Road", soundcloud));
        QVERIFY(!pm.addToPlaylist("Nope", yandex));
        // без названия песню не узнать: такие треки сравниваются только по id
        QVERIFY(pm.addToPlaylist("Road", QVariantMap{{"id", "2"}}));
        QVERIFY(pm.addToPlaylist("Road", QVariantMap{{"id", "3"}}));

        QCOMPARE(pm.playlistsWith(soundcloud), QStringList({"Road"}));
        QVERIFY(pm.playlistsWith(track("9")).isEmpty());
    }

    // правки с телефона приходят составом целиком
    void wholeTrackListIsReplacedWithoutRepeats() {
        QJsonObject data;
        PlaylistManager pm(&data);
        pm.createPlaylistWithTracks("Night", "", {track("1"), track("2")});
        QSignalSpy changed(&pm, &PlaylistManager::playlistsChanged);

        QVariantMap again{{"id", "s9"}, {"service", "SoundCloud"}, {"title", "title 3"}};
        QVERIFY(pm.setPlaylistTracks("Night", {track("3"), track("1"), again, track("1")}));
        QStringList ids;
        for (const QVariant& t : pm.getPlaylists()["Night"].toMap()["tracks"].toList())
            ids << t.toMap()["id"].toString();
        QCOMPARE(ids, QStringList({"3", "1"}));
        QCOMPARE(changed.count(), 1);
        QVERIFY(!pm.setPlaylistTracks("Nope", {track("1")}));
    }

    void mergeTakesServiceAsSourceOfTruth() {
        QJsonObject data;
        QJsonArray likes;
        likes.append(QJsonObject::fromVariantMap(track("1")));
        likes.append(QJsonObject::fromVariantMap(track("2")));
        likes.append(QJsonObject::fromVariantMap(track("yt", "YouTube")));
        likes.append(QJsonObject{{"id", "old"}, {"title", "liked before services were stored"}});
        data["likes"] = likes;
        PlaylistManager pm(&data);

        // с телефона лайкнули 3, сняли 2
        pm.mergeServiceLikes("Yandex", {track("3"), track("1")}, {"3", "1"});

        QCOMPARE(likedIds(pm), QStringList({"3", "1", "yt", "old"}));
        QVERIFY(pm.isLiked("3"));
        QVERIFY(!pm.isLiked("2"));
    }

    void mergeKeepsLikesServiceNoLongerDescribes() {
        QJsonObject data;
        QJsonArray likes;
        likes.append(QJsonObject::fromVariantMap(track("removed")));
        data["likes"] = likes;
        PlaylistManager pm(&data);

        // id в лайках есть, а данных по нему сервис уже не отдаёт
        pm.mergeServiceLikes("Yandex", {track("1")}, {"1", "removed"});

        QCOMPARE(likedIds(pm), QStringList({"1", "removed"}));
    }
};

QTEST_GUILESS_MAIN(TestPlaylistManager)
#include "tst_playlist_manager.moc"
