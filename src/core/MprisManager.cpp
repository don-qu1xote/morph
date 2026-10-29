#include "MprisManager.h"
#include "../audio/AudioEngine.h"

#ifdef Q_OS_MACOS
#include "MacNowPlaying.h"
#else
#include <QtDBus/QDBusConnection>
#endif

MprisManager::MprisManager(AudioEngine* audio, QObject* parent) : QObject(parent) {
#ifdef Q_OS_MACOS
    m_nowPlaying = new MacNowPlaying(audio, this);
    connect(m_nowPlaying, &MacNowPlaying::nextRequested, this, &MprisManager::nextRequested);
    connect(m_nowPlaying, &MacNowPlaying::previousRequested, this, &MprisManager::previousRequested);
#else
    m_root = new MprisRootAdaptor(this);
    m_player = new MprisPlayerAdaptor(audio, this);

    connect(m_player, &MprisPlayerAdaptor::nextRequested, this, &MprisManager::nextRequested);
    connect(m_player, &MprisPlayerAdaptor::previousRequested, this, &MprisManager::previousRequested);
    connect(m_player, &MprisPlayerAdaptor::metadataChanged, this, &MprisManager::metadataChanged);

    QDBusConnection::sessionBus().registerService("org.mpris.MediaPlayer2.morph");
    QDBusConnection::sessionBus().registerObject("/org/mpris/MediaPlayer2", this);
#endif
}

void MprisManager::updateMetadata(const QVariantMap& track) {
#ifdef Q_OS_MACOS
    m_track = track;
    m_nowPlaying->setTrack(track);
    emit metadataChanged();
#else
    m_player->updateMetadata(track);
#endif
}

QString MprisManager::currentTitle() const {
#ifdef Q_OS_MACOS
    return m_track.value("title").toString();
#else
    return m_player->Metadata().value("xesam:title").toString();
#endif
}

QString MprisManager::currentArtist() const {
#ifdef Q_OS_MACOS
    return m_track.value("artist").toString();
#else
    QStringList artists = m_player->Metadata().value("xesam:artist").toStringList();
    return artists.isEmpty() ? "" : artists.first();
#endif
}
