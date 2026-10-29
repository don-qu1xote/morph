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
};

QTEST_GUILESS_MAIN(TestPlaylistManager)
#include "tst_playlist_manager.moc"
