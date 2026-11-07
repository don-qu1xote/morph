#include "PlaylistSync.h"
#include "../utils/BackgroundWriter.h"
#include "Library.h"
#include "TrackMatch.h"
#include "../services/YandexService.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <memory>

// поисков в Яндексе на плейлист за раз: остальное найдётся при следующей сверке
static const int kSearchesPerPlaylist = 20;
static const double kSameSong = 0.85;

static QStringList idsOnly(const QStringList& fullIds) {
    QStringList out;
    for (const QString& full : fullIds) out.append(full.section(':', 0, 0));
    return out;
}

PlaylistSync::PlaylistSync(YandexService* yandex, Library* library, const QString& path, QObject* parent)
    : QObject(parent), yandex(yandex), library(library), m_path(path) {
    load();
}

void PlaylistSync::setEnabled(bool enabled) {
    bool turnedOn = enabled && !m_enabled;
    m_enabled = enabled;
    if (turnedOn) sync(m_playlists);
}

void PlaylistSync::sync(const QVariantMap& playlists) {
    m_playlists = playlists;
    if (!m_enabled) return;
    m_queue = playlists.keys();
    next();
}

void PlaylistSync::renamed(const QString& oldName, const QString& newName) {
    QString kind = m_kinds.take(oldName);
    if (kind.isEmpty()) return;
    m_kinds.insert(newName, kind);
    save();
}

void PlaylistSync::next() {
    if (m_busy || !m_enabled || !yandex->hasToken()) return;

    // плейлист удалили в morph: тот, что morph завёл в Яндексе, уходит тоже
    for (auto it = m_kinds.cbegin(); it != m_kinds.cend(); ++it) {
        if (m_playlists.contains(it.key())) continue;
        m_busy = true;
        yandex->deletePlaylist(it.value(), [this, name = it.key()](bool) {
            m_kinds.remove(name);
            save();
            m_busy = false;
            next();
        });
        return;
    }

    if (m_queue.isEmpty()) return;
    QString name = m_queue.takeFirst();
    m_busy = true;
    syncOne(name, m_playlists.value(name).toMap()["tracks"].toList());
}

void PlaylistSync::syncOne(const QString& name, const QVariantList& tracks) {
    int total = tracks.size();
    resolve(tracks, [this, name, total](const QStringList& ids) {
        yandex->withAlbumIds(ids, [this, name, total](const QStringList& full) {
            QString kind = m_kinds.value(name);
            if (!kind.isEmpty()) {
                apply(name, kind, full, total, false);
                return;
            }
            yandex->createPlaylist(name, [this, name, full, total](const QString& created) {
                if (created.isEmpty()) {
                    m_busy = false;
                    next();
                    return;
                }
                m_kinds.insert(name, created);
                save();
                apply(name, created, full, total, true);
            });
        });
    });
}

// Сначала треки, потом имя: переименование поднимает ревизию, и изменение
// треков со старой ревизией Яндекс бы отклонил
void PlaylistSync::apply(const QString& name, const QString& kind, const QStringList& tracks, int total, bool retried) {
    yandex->playlistState(kind, [=, this](bool ok, int revision, const QString& title, const QStringList& current) {
        if (!ok) {
            // плейлист удалили в Яндексе руками: заводим заново, но один раз
            m_kinds.remove(name);
            save();
            if (retried) {
                m_busy = false;
                next();
                return;
            }
            yandex->createPlaylist(name, [=, this](const QString& created) {
                if (created.isEmpty()) {
                    m_busy = false;
                    next();
                    return;
                }
                m_kinds.insert(name, created);
                save();
                apply(name, created, tracks, total, true);
            });
            return;
        }

        auto finish = [=, this]() {
            emit synced(name, tracks.size(), total);
            m_busy = false;
            next();
        };
        auto rename = [=, this]() {
            if (title == name) finish();
            else yandex->renamePlaylist(kind, name, [finish](bool) { finish(); });
        };
        if (idsOnly(current) == idsOnly(tracks)) rename();
        else yandex->replacePlaylistTracks(kind, revision, current.size(), tracks, [rename](bool) { rename(); });
    });
}

// Треки из Яндекса как есть, остальные через связи MAIN, найденное раньше
// или поиск. Порядок плейлиста сохраняется, повторы и ненайденное выпадают
void PlaylistSync::resolve(const QVariantList& tracks, std::function<void(const QStringList&)> done) {
    struct State {
        QStringList ids;
        int pending = 0;
    };
    auto state = std::make_shared<State>();
    QList<int> toSearch;
    for (int i = 0; i < tracks.size(); ++i) {
        QVariantMap t = tracks[i].toMap();
        QString key = TrackMatch::songKey(t);
        QString id;
        if (t["service"].toString() == "Yandex") id = t["id"].toString();
        if (id.isEmpty()) id = library->yandexId(key);
        if (id.isEmpty()) id = m_found.value(key);
        state->ids.append(id);
        if (id.isEmpty() && !m_found.contains(key) && toSearch.size() < kSearchesPerPlaylist) toSearch.append(i);
    }

    auto finish = [state, done]() {
        QStringList out;
        QSet<QString> seen;
        for (const QString& id : std::as_const(state->ids)) {
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            out.append(id);
        }
        done(out);
    };
    if (toSearch.isEmpty()) {
        finish();
        return;
    }

    state->pending = toSearch.size();
    for (int i : std::as_const(toSearch)) {
        QVariantMap t = tracks[i].toMap();
        yandex->searchTracks(TrackMatch::searchQuery(t), [this, state, i, t, finish](const QVariantList& found) {
            QString id;
            double bestScore = kSameSong;
            for (const QVariant& v : found) {
                double score = TrackMatch::score(v.toMap(), t);
                if (score >= bestScore) {
                    bestScore = score;
                    id = v.toMap()["id"].toString();
                }
            }
            m_found.insert(TrackMatch::songKey(t), id);
            state->ids[i] = id;
            if (--state->pending > 0) return;
            save();
            finish();
        });
    }
}

void PlaylistSync::load() {
    BackgroundWriter::waitForAll();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    QJsonObject kinds = saved["kinds"].toObject();
    for (auto it = kinds.begin(); it != kinds.end(); ++it) m_kinds.insert(it.key(), it.value().toString());
    QJsonObject found = saved["found"].toObject();
    for (auto it = found.begin(); it != found.end(); ++it) m_found.insert(it.key(), it.value().toString());
}

void PlaylistSync::save() const {
    QJsonObject kinds;
    for (auto it = m_kinds.cbegin(); it != m_kinds.cend(); ++it) kinds.insert(it.key(), it.value());
    QJsonObject found;
    for (auto it = m_found.cbegin(); it != m_found.cend(); ++it) found.insert(it.key(), it.value());
    BackgroundWriter::write(m_path, QJsonObject{{"kinds", kinds}, {"found", found}});
}
