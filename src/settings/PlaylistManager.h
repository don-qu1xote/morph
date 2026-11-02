#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>

#include <QSet>
#include <QStringList>

class PlaylistManager : public QObject {
    Q_OBJECT
public:
    explicit PlaylistManager(QJsonObject* data, QObject* parent = nullptr);

    QVariantList toggleLike(const QVariantMap& track);
    bool isLiked(const QString& trackId);
    QVariantList getLikedTracks();
    void mergeServiceLikes(const QString& service, const QVariantList& tracks, const QStringList& allIds);
    // только добавить: для площадок, куда morph писать не может. atTop: новые
    // лайки наверх, иначе в конец (первый импорт старых лайков). Сколько добавлено
    int importLikes(const QVariantList& tracks, bool atTop);

    void createPlaylist(const QString& name, const QString& coverUrl = "");
    void createPlaylistWithTracks(const QString& name, const QString& coverUrl, const QVariantList& tracks);
    void renamePlaylist(const QString& oldName, const QString& newName, const QString& coverUrl);
    void deletePlaylist(const QString& name);
    void addToPlaylist(const QString& playlistName, const QVariantMap& track);
    void removeFromPlaylist(const QString& playlistName, const QString& trackId);
    QVariantMap getPlaylists();

signals:
    void likesChanged();
    void playlistsChanged();

private:
    QJsonObject* m_data;
    QSet<QString> m_likedSet;
    void updateLikedSet();
};

#endif
