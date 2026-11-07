#include "Library.h"
#include "TrackMatch.h"
#include "../utils/BackgroundWriter.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

// Яндекс лучше по качеству и без VPN, SoundCloud без VPN, YouTube только с ним
static const QStringList kServiceOrder = {"Yandex", "SoundCloud", "YouTube"};
// не нашлось в Яндексе: следующая попытка через неделю, вдруг трек добавят
static const qint64 kRetryLinkSecs = 7 * 24 * 3600;

Library::Library(const QString& path, QObject* parent) : QObject(parent), m_path(path) {
    load();
}

Library::~Library() {
    BackgroundWriter::waitForAll();
}

void Library::syncLikes(const QVariantList& likes) {
    m_songs.clear();
    m_order.clear();
    m_keyOf.clear();

    for (const QVariant& v : likes) {
        QVariantMap track = v.toMap();
        QString id = track["id"].toString();
        if (id.isEmpty()) continue;
        if (track["service"].toString().isEmpty()) track["service"] = "Yandex";

        QString key = TrackMatch::songKey(track);
        // без названия песню не с чем сравнить, она остаётся отдельной записью
        if (key.endsWith('|')) key = "id:" + id;

        if (!m_songs.contains(key)) {
            m_order.append(key);
            m_songs[key].linked = m_links.value(key);
        }
        m_songs[key].liked.append(track);
        m_keyOf.insert(id, key);
    }
    for (const QString& key : std::as_const(m_order)) {
        for (const QVariantMap& t : std::as_const(m_songs[key].linked)) m_keyOf.insert(t["id"].toString(), key);
    }
    emit changed();
}

QVariantList Library::tracks() const {
    QVariantList out;
    for (const QString& key : m_order) {
        const Song& song = m_songs[key];
        QVariantMap track = best(song);
        QStringList also;
        for (const QVariantMap& s : allSources(song)) {
            QString service = s["service"].toString();
            if (service != track["service"].toString() && !also.contains(service)) also.append(service);
        }
        track["alsoOn"] = also.join(",");
        track["songKey"] = key;
        out.append(track);
    }
    return out;
}

QVariantMap Library::summary(int covers) const {
    QVariantList coverUrls;
    QVariantMap sources;
    for (const QString& key : m_order) {
        QVariantMap track = best(m_songs[key]);
        QString service = track["service"].toString();
        sources[service] = sources.value(service).toInt() + 1;
        QString cover = track["coverUrl"].toString();
        if (coverUrls.size() < covers && !cover.isEmpty()) coverUrls.append(cover);
    }
    return {{"count", int(m_order.size())}, {"covers", coverUrls}, {"sources", sources}};
}

QStringList Library::sourcesOf(const QString& trackId) const {
    auto key = m_keyOf.constFind(trackId);
    if (key == m_keyOf.constEnd()) return {};

    QStringList services;
    QVariantMap own = trackById(trackId);
    services.append(own["service"].toString());
    for (const QVariantMap& s : allSources(m_songs[*key])) {
        QString service = s["service"].toString();
        if (!services.contains(service)) services.append(service);
    }
    return services;
}

QVariantMap Library::trackById(const QString& trackId) const {
    auto key = m_keyOf.constFind(trackId);
    if (key == m_keyOf.constEnd()) return {};
    for (const QVariantMap& s : allSources(m_songs[*key])) {
        if (s["id"].toString() == trackId) return s;
    }
    return {};
}

QList<QVariantMap> Library::alternatives(const QString& trackId) const {
    auto key = m_keyOf.constFind(trackId);
    if (key == m_keyOf.constEnd()) return {};

    QString ownService = trackById(trackId)["service"].toString();
    QList<QVariantMap> out;
    for (const QVariantMap& s : allSources(m_songs[*key])) {
        if (s["service"].toString() == ownService || rank(s) >= kServiceOrder.size()) continue;
        out.append(slim(s));
    }
    std::stable_sort(out.begin(), out.end(),
                     [this](const QVariantMap& a, const QVariantMap& b) { return rank(a) < rank(b); });
    return out;
}

QString Library::sourceId(const QString& songKey, const QString& service) const {
    return source(songKey, service).value("id").toString();
}

QVariantMap Library::source(const QString& songKey, const QString& service) const {
    auto song = m_songs.constFind(songKey);
    if (song != m_songs.constEnd()) {
        for (const QVariantMap& t : song->liked) {
            if (t["service"].toString() == service) return t;
        }
    }
    return m_links.value(songKey).value(service);
}

// у Яндекса ключ без префикса: так он записан с первой версии library.json
static QString triedKey(const QString& songKey, const QString& service) {
    return service == "Yandex" ? songKey : service + "|" + songKey;
}

QString Library::nextToLink(const QString& service) const {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QString& key : m_order) {
        if (key.startsWith("id:") || !sourceId(key, service).isEmpty()) continue;
        if (now - m_tried.value(triedKey(key, service), 0) < kRetryLinkSecs) continue;
        return key;
    }
    return {};
}

void Library::setMirrored(const QString& service, const QString& songKey, bool on) {
    QStringList& keys = m_mirrored[service];
    if (on == keys.contains(songKey)) return;
    if (on) keys.append(songKey);
    else keys.removeAll(songKey);
    save();
}

QVariantMap Library::representative(const QString& songKey) const {
    auto song = m_songs.constFind(songKey);
    return song == m_songs.constEnd() ? QVariantMap() : best(*song);
}

void Library::markLinkTried(const QString& songKey, const QString& service) {
    m_tried[triedKey(songKey, service)] = QDateTime::currentSecsSinceEpoch();
    save();
}

void Library::link(const QString& songKey, const QVariantMap& track) {
    QVariantMap saved = slim(track);
    QString service = saved["service"].toString();
    m_links[songKey][service] = saved;
    m_tried.remove(triedKey(songKey, service));
    save();

    auto song = m_songs.find(songKey);
    if (song != m_songs.end()) {
        song->linked[service] = saved;
        m_keyOf.insert(saved["id"].toString(), songKey);
    }
}

// чем меньше, тем надёжнее играет; версия, которую сервис не отдаёт, в конце
int Library::rank(const QVariantMap& track) const {
    if (m_available && !m_available(track)) return kServiceOrder.size();
    int index = kServiceOrder.indexOf(track["service"].toString());
    return index < 0 ? kServiceOrder.size() - 1 : index;
}

QVariantMap Library::best(const Song& song) const {
    const QVariantMap* found = &song.liked.first();
    for (const QVariantMap& t : song.liked) {
        if (rank(t) < rank(*found)) found = &t;
    }
    return *found;
}

QList<QVariantMap> Library::allSources(const Song& song) const {
    QList<QVariantMap> out = song.liked;
    for (const QVariantMap& t : song.linked) out.append(t);
    return out;
}

QVariantMap Library::slim(const QVariantMap& track) {
    static const QStringList fields = {"id", "service", "artist", "title", "durationMs", "coverUrl", "album", "webUrl"};
    QVariantMap out;
    for (const QString& f : fields) {
        if (track.contains(f)) out[f] = track[f];
    }
    return out;
}

void Library::load() {
    BackgroundWriter::waitForAll();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();

    QJsonObject links = saved["links"].toObject();
    for (auto it = links.begin(); it != links.end(); ++it) {
        QJsonObject byService = it.value().toObject();
        for (auto s = byService.begin(); s != byService.end(); ++s)
            m_links[it.key()][s.key()] = s.value().toObject().toVariantMap();
    }
    QJsonObject tried = saved["tried"].toObject();
    for (auto it = tried.begin(); it != tried.end(); ++it) m_tried.insert(it.key(), it.value().toInteger());
    QJsonObject mirrored = saved["mirrored"].toObject();
    for (auto it = mirrored.begin(); it != mirrored.end(); ++it) {
        for (const auto& key : it.value().toArray()) m_mirrored[it.key()].append(key.toString());
    }
}

void Library::save() const {
    QJsonObject links;
    for (auto it = m_links.cbegin(); it != m_links.cend(); ++it) {
        QJsonObject byService;
        for (auto s = it->cbegin(); s != it->cend(); ++s) byService.insert(s.key(), QJsonObject::fromVariantMap(*s));
        links.insert(it.key(), byService);
    }
    QJsonObject tried;
    for (auto it = m_tried.cbegin(); it != m_tried.cend(); ++it) tried.insert(it.key(), it.value());
    QJsonObject mirrored;
    for (auto it = m_mirrored.cbegin(); it != m_mirrored.cend(); ++it)
        mirrored.insert(it.key(), QJsonArray::fromStringList(*it));

    BackgroundWriter::write(m_path, QJsonObject{{"links", links}, {"tried", tried}, {"mirrored", mirrored}});
}
