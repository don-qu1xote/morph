#include "MorphBackend.h"
#include "PhoneMixes.h"
#include "ServerAudio.h"
#include "../core/ServiceManager.h"
#include "../core/TrackMatch.h"
#include "../network/NetworkManager.h"
#include "../settings/SettingsManager.h"
#include "../utils/CacheManager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSaveFile>

MorphBackend::MorphBackend(SettingsManager* settings, ServiceManager* services, CacheManager* cache,
                           NetworkManager* net, ServerAudio* audio, PhoneMixes* mixes, const QString& coverDir,
                           QObject* parent)
    : QObject(parent), m_settings(settings), m_services(services), m_cache(cache), m_net(net), m_audio(audio),
      m_mixes(mixes), m_coverDir(coverDir) {
    QDir().mkpath(m_coverDir);
    auto bump = [this] { ++m_revision; };
    connect(services->library(), &Library::changed, this, bump);
    connect(settings, &SettingsManager::playlistsChanged, this, bump);
    connect(mixes, &PhoneMixes::changed, this, bump);
}

QVariantList MorphBackend::library() const {
    return m_services->library()->tracks();
}

QStringList MorphBackend::playlistNames() const {
    return m_settings->playlistNames();
}

QVariantList MorphBackend::playlistTracks(const QString& name) const {
    return m_settings->playlistTracks(name);
}

QString MorphBackend::playlistCover(const QString& name) const {
    return m_settings->playlistCover(name);
}

QVariantList MorphBackend::mixes() const {
    return m_mixes->mixes();
}

// лайк это песня в MAIN, в какой бы версии её ни лайкнули
bool MorphBackend::isLiked(const QVariantMap& track) const {
    QString key = track.value("songKey").toString();
    return m_services->library()->hasSong(key.isEmpty() ? TrackMatch::songKey(track) : key);
}

void MorphBackend::setLiked(const QVariantMap& track, bool liked) {
    // toggleLike снимает все версии песни сразу, поэтому только при расхождении
    if (isLiked(track) != liked) m_settings->toggleLike(track);
}

void MorphBackend::listened(const QVariantMap& track, double played, double length, bool completed) {
    m_services->phoneListen(track, played, length, completed);
}

QString MorphBackend::createPlaylist(const QString& name, const QVariantList& tracks) {
    return m_settings->createPlaylistWithTracks(name, "", tracks);
}

bool MorphBackend::updatePlaylist(const QString& name, const QString& newName, const QVariantList& tracks) {
    QString target = name;
    if (newName != name) {
        if (!m_settings->renamePlaylist(name, newName, m_settings->playlistCover(name))) return false;
        target = newName;
    }
    return m_settings->setPlaylistTracks(target, tracks);
}

void MorphBackend::deletePlaylist(const QString& name) {
    m_settings->deletePlaylist(name);
}

SubsonicBackend::AudioInfo MorphBackend::audioInfo(const QVariantMap& track) const {
    return m_audio->info(track);
}

void MorphBackend::audio(const QVariantMap& track, AudioDone done) {
    m_audio->fetch(track, std::move(done));
}

// Обложки, которых нет в кэше окна, качаются в свою папку: кэш обложек можно
// выключить в настройках, а телефону они нужны всё равно
void MorphBackend::cover(const QString& url, CoverDone done) {
    QString cached = m_cache->getCachedCover(url);
    if (cached.startsWith("file://")) {
        done(QUrl(cached).toLocalFile());
        return;
    }
    QString path = m_coverDir + "/" + QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex();
    if (QFileInfo(path).size() > 0) {
        done(path);
        return;
    }
    auto& waiting = m_covers[url];
    waiting.append(std::move(done));
    if (waiting.size() > 1) return;

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply* reply = m_net->getNetworkAccessManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, path] {
        QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        reply->deleteLater();
        bool saved = false;
        if (!data.isEmpty()) {
            QSaveFile file(path);
            saved = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
        }
        const auto waiters = m_covers.take(url);
        for (const auto& w : waiters) w(saved ? path : QString());
    });
}
