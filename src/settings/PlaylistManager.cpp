#include "PlaylistManager.h"
#include "../core/TrackMatch.h"
#include <algorithm>

PlaylistManager::PlaylistManager(QJsonObject* data, QObject* parent)
    : QObject(parent), m_data(data) {}

void PlaylistManager::updateLikedSet() {
    m_likedSet.clear();
    if (!m_data->contains("likes")) return;
    QJsonArray likes = (*m_data)["likes"].toArray();
    for (int i = 0; i < likes.size(); ++i) {
        m_likedSet.insert(likes[i].toObject()["id"].toString());
    }
}

// Снятый лайк снимается со всех версий песни: в MAIN она одна запись, и
// оставшийся лайк на другой площадке вернул бы её обратно. Возвращает снятые
QVariantList PlaylistManager::toggleLike(const QVariantMap& track) {
    QJsonArray likes = (*m_data)["likes"].toArray();
    QString id = track["id"].toString();
    QString key = TrackMatch::songKey(track);
    bool liked = std::any_of(likes.begin(), likes.end(),
                             [&id](const QJsonValue& v) { return v.toObject()["id"].toString() == id; });

    QVariantList removed;
    if (liked) {
        QJsonArray kept;
        for (const auto& v : std::as_const(likes)) {
            QVariantMap other = v.toObject().toVariantMap();
            bool sameSong = other["id"].toString() == id || (!key.endsWith('|') && TrackMatch::songKey(other) == key);
            if (sameSong) removed.append(other);
            else kept.append(v);
        }
        likes = kept;
    } else {
        likes.insert(0, QJsonObject::fromVariantMap(track));
    }
    (*m_data)["likes"] = likes;
    updateLikedSet();
    emit likesChanged();
    return removed;
}

bool PlaylistManager::isLiked(const QString& trackId) {
    if (m_likedSet.isEmpty() && m_data->contains("likes")) {
        updateLikedSet();
    }
    return m_likedSet.contains(trackId);
}

QVariantList PlaylistManager::getLikedTracks() {
    return (*m_data)["likes"].toArray().toVariantList();
}

// Для лайков сервиса правда на стороне сервиса: лайкнул с телефона, тут появилось,
// снял, исчезло. Лайки других сервисов (ютуб, саундклауд) не трогаем.
// Кого нет в tracks, но есть в allIds, оставляем: это как раз треки, убранные из
// каталога, по которым сервис уже не отдаёт данных
void PlaylistManager::mergeServiceLikes(const QString& service, const QVariantList& tracks, const QStringList& allIds) {
    QSet<QString> remote(allIds.begin(), allIds.end());
    for (const QVariant& t : tracks) remote.insert(t.toMap()["id"].toString());

    QJsonArray kept;
    QSet<QString> local;
    for (const auto& v : (*m_data)["likes"].toArray()) {
        QJsonObject obj = v.toObject();
        QString id = obj["id"].toString();
        if (obj["service"].toString() == service && !remote.contains(id)) continue;
        kept.append(obj);
        local.insert(id);
    }

    // новых лайков с сервера обычно немного и они самые свежие, поэтому наверх
    QJsonArray merged;
    for (const QVariant& t : tracks) {
        QVariantMap track = t.toMap();
        if (!local.contains(track["id"].toString())) merged.append(QJsonObject::fromVariantMap(track));
    }
    for (const auto& v : kept) merged.append(v);

    (*m_data)["likes"] = merged;
    updateLikedSet();
    emit likesChanged();
}

int PlaylistManager::importLikes(const QVariantList& tracks, bool atTop) {
    QJsonArray likes = (*m_data)["likes"].toArray();
    QSet<QString> known;
    for (const auto& v : std::as_const(likes)) {
        QVariantMap t = v.toObject().toVariantMap();
        known.insert(t["id"].toString());
        known.insert(TrackMatch::songKey(t));
    }

    QJsonArray added;
    for (const QVariant& v : tracks) {
        QVariantMap t = v.toMap();
        QString key = TrackMatch::songKey(t);
        // та же песня уже в MAIN с другой площадки, второй лайк ей ни к чему
        if (known.contains(t["id"].toString()) || known.contains(key)) continue;
        known.insert(key);
        added.append(QJsonObject::fromVariantMap(t));
    }
    if (added.isEmpty()) return 0;

    QJsonArray merged = atTop ? added : likes;
    for (const auto& v : atTop ? std::as_const(likes) : std::as_const(added)) merged.append(v);
    (*m_data)["likes"] = merged;
    updateLikedSet();
    emit likesChanged();
    return added.size();
}

QString PlaylistManager::uniqueName(const QString& name) const {
    QString base = name.trimmed();
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (!playlists.contains(base)) return base;
    for (int n = 2;; ++n) {
        QString candidate = QString("%1 %2").arg(base).arg(n);
        if (!playlists.contains(candidate)) return candidate;
    }
}

QString PlaylistManager::defaultName() const {
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    for (int n = 1;; ++n) {
        QString candidate = QString("My Playlist #%1").arg(n);
        if (!playlists.contains(candidate)) return candidate;
    }
}

bool PlaylistManager::createPlaylist(const QString& name, const QString& coverUrl) {
    QString clean = name.trimmed();
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (clean.isEmpty() || playlists.contains(clean)) return false;

    QJsonObject playlistData;
    playlistData["coverUrl"] = coverUrl;
    playlistData["tracks"] = QJsonArray();
    playlists[clean] = playlistData;
    (*m_data)["playlists"] = playlists;
    emit playlistsChanged();
    return true;
}

QString PlaylistManager::createPlaylistWithTracks(const QString& name, const QString& coverUrl,
                                                  const QVariantList& tracks, const QString& source) {
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    QString clean = name.trimmed().isEmpty() ? defaultName() : name.trimmed();
    bool refresh = !source.isEmpty() && playlists[clean].toObject()["source"].toString() == source;
    QString target = refresh ? clean : uniqueName(clean);

    QJsonObject playlistData;
    playlistData["coverUrl"] = coverUrl;
    if (!source.isEmpty()) playlistData["source"] = source;

    QJsonArray tracksArray;
    for (const QVariant& t : tracks) tracksArray.append(QJsonObject::fromVariantMap(t.toMap()));
    playlistData["tracks"] = tracksArray;

    playlists[target] = playlistData;
    (*m_data)["playlists"] = playlists;
    emit playlistsChanged();
    return target;
}

bool PlaylistManager::renamePlaylist(const QString& oldName, const QString& newName, const QString& coverUrl) {
    QString clean = newName.trimmed();
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (!playlists.contains(oldName) || clean.isEmpty()) return false;
    if (clean != oldName && playlists.contains(clean)) return false;

    QJsonObject playlistData = playlists[oldName].toObject();
    playlistData["coverUrl"] = coverUrl;
    playlists.remove(oldName);
    playlists[clean] = playlistData;
    (*m_data)["playlists"] = playlists;
    emit playlistsChanged();
    return true;
}

void PlaylistManager::deletePlaylist(const QString& name) {
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (playlists.contains(name)) {
        playlists.remove(name);
        (*m_data)["playlists"] = playlists;
        emit playlistsChanged();
    }
}

bool PlaylistManager::addToPlaylist(const QString& playlistName, const QVariantMap& track) {
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (!playlists.contains(playlistName)) return false;
    QJsonObject playlistData = playlists[playlistName].toObject();
    QJsonArray tracks = playlistData["tracks"].toArray();
    for (const auto& t : std::as_const(tracks)) {
        if (sameSong(t.toObject().toVariantMap(), track)) return false;
    }
    tracks.insert(0, QJsonObject::fromVariantMap(track));
    playlistData["tracks"] = tracks;
    playlists[playlistName] = playlistData;
    (*m_data)["playlists"] = playlists;
    emit playlistsChanged();
    return true;
}

QStringList PlaylistManager::playlistsWith(const QVariantMap& track) const {
    QStringList out;
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    for (auto it = playlists.begin(); it != playlists.end(); ++it) {
        const QJsonArray tracks = it.value().toObject()["tracks"].toArray();
        for (const auto& t : tracks) {
            if (sameSong(t.toObject().toVariantMap(), track)) {
                out.append(it.key());
                break;
            }
        }
    }
    return out;
}

// Та же запись или другая версия той же песни. Ключ без названия ничего не
// говорит, по нему не сравниваем
bool PlaylistManager::sameSong(const QVariantMap& a, const QVariantMap& b) {
    if (!a["id"].toString().isEmpty() && a["id"].toString() == b["id"].toString()) return true;
    QString key = TrackMatch::songKey(a);
    return !key.endsWith('|') && key == TrackMatch::songKey(b);
}

void PlaylistManager::removeFromPlaylist(const QString& playlistName, const QString& trackId) {
    QJsonObject playlists = (*m_data)["playlists"].toObject();
    if (!playlists.contains(playlistName)) return;

    QJsonObject playlistData = playlists[playlistName].toObject();
    QJsonArray tracks = playlistData["tracks"].toArray();

    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].toObject()["id"].toString() == trackId) {
            tracks.removeAt(i);
            break;
        }
    }

    playlistData["tracks"] = tracks;
    playlists[playlistName] = playlistData;
    (*m_data)["playlists"] = playlists;
    emit playlistsChanged();
}

QVariantMap PlaylistManager::getPlaylists() {
    if (!m_data->contains("playlists") || !(*m_data)["playlists"].isObject()) return QVariantMap();
    return (*m_data)["playlists"].toObject().toVariantMap();
}
