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
