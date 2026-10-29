#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "MacNowPlaying.h"
#include "../audio/AudioEngine.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

MacNowPlaying::MacNowPlaying(AudioEngine* audio, QObject* parent)
    : QObject(parent), m_audio(audio), m_network(new QNetworkAccessManager(this)) {
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];

    // хэндлеры зовутся на главном потоке, так что дёргаем движок напрямую
    [center.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        m_audio->resume();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        m_audio->pause();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        if (m_audio->isPlaying()) m_audio->pause();
        else m_audio->resume();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        emit nextRequested();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        emit previousRequested();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.changePlaybackPositionCommand
        addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            MPChangePlaybackPositionCommandEvent* seek = (MPChangePlaybackPositionCommandEvent*)event;
            m_audio->setPosition(static_cast<qint64>(seek.positionTime * 1000));
            return MPRemoteCommandHandlerStatusSuccess;
        }];

    // позицию каждый тик не шлём: система сама досчитывает время по rate
    connect(m_audio, &AudioEngine::stateChanged, this, &MacNowPlaying::publish);
    connect(m_audio, &AudioEngine::durationChanged, this, &MacNowPlaying::publish);
    connect(m_audio, &AudioEngine::seeked, this, &MacNowPlaying::publish);
}

MacNowPlaying::~MacNowPlaying() {
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    [center.playCommand removeTarget:nil];
    [center.pauseCommand removeTarget:nil];
    [center.togglePlayPauseCommand removeTarget:nil];
    [center.nextTrackCommand removeTarget:nil];
    [center.previousTrackCommand removeTarget:nil];
    [center.changePlaybackPositionCommand removeTarget:nil];
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
}

void MacNowPlaying::setTrack(const QVariantMap& track) {
    m_track = track;

    QString coverUrl = track.value("coverUrl").toString();
    if (coverUrl != m_coverUrl) {
        m_coverUrl = coverUrl;
        m_cover.clear();
        if (!coverUrl.isEmpty()) loadCover(coverUrl);
    }
    publish();
}

void MacNowPlaying::loadCover(const QString& url) {
    QNetworkReply* reply = m_network->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        // пока качали, трек могли переключить, тогда обложка уже чужая
        if (url != m_coverUrl || reply->error() != QNetworkReply::NoError) return;
        m_cover = reply->readAll();
        publish();
    });
}

void MacNowPlaying::publish() {
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = m_track.value("title").toString().toNSString();
    info[MPMediaItemPropertyArtist] = m_track.value("artist").toString().toNSString();
    info[MPMediaItemPropertyAlbumTitle] = m_track.value("album").toString().toNSString();
    info[MPMediaItemPropertyPlaybackDuration] = @(m_audio->duration() / 1000.0);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(m_audio->position() / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(m_audio->isPlaying() ? 1.0 : 0.0);

    if (!m_cover.isEmpty()) {
        NSImage* image = [[NSImage alloc] initWithData:m_cover.toNSData()];
        if (image) {
            info[MPMediaItemPropertyArtwork] =
                [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                requestHandler:^NSImage*(CGSize size) { return image; }];
        }
    }

    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = info;
    center.playbackState = m_audio->isPlaying() ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}
