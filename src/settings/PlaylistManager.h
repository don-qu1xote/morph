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

    // Свободное имя: само name, если его нет, иначе «name 2», «name 3» и дальше
    QString uniqueName(const QString& name) const;
    // имя для плейлиста, который создают без названия: My Playlist #N
    QString defaultName() const;
    // false, если имя пустое или занято: молча затереть другой плейлист нельзя
    bool createPlaylist(const QString& name, const QString& coverUrl = "");
    // Плейлист сразу с треками: импорт и SAVE AS PLAYLIST. Повторный импорт той
    // же ссылки (source) обновляет прежний плейлист, в остальных случаях занятое
    // имя получает номер. Возвращает имя, под которым плейлист сохранён
    QString createPlaylistWithTracks(const QString& name, const QString& coverUrl, const QVariantList& tracks,
                                     const QString& source = "");
    // false, если newName занят другим плейлистом
    bool renamePlaylist(const QString& oldName, const QString& newName, const QString& coverUrl);
    void deletePlaylist(const QString& name);
    // false, если эта песня уже есть в плейлисте, хоть в другой версии
    bool addToPlaylist(const QString& playlistName, const QVariantMap& track);
    void removeFromPlaylist(const QString& playlistName, const QString& trackId);
    QVariantMap getPlaylists();
    // плейлисты, в которых уже есть эта песня
    QStringList playlistsWith(const QVariantMap& track) const;

signals:
    void likesChanged();
    void playlistsChanged();

private:
    static bool sameSong(const QVariantMap& a, const QVariantMap& b);
    QJsonObject* m_data;
    QSet<QString> m_likedSet;
    void updateLikedSet();
};

#endif
