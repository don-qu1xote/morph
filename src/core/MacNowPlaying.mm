#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "MacNowPlaying.h"
#include "../audio/AudioEngine.h"

MacNowPlaying::MacNowPlaying(AudioEngine* audio, QObject* parent) : QObject(parent), m_audio(audio) {
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
    publish();
}

void MacNowPlaying::publish() {
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = m_track.value("title").toString().toNSString();
    info[MPMediaItemPropertyArtist] = m_track.value("artist").toString().toNSString();
    info[MPMediaItemPropertyAlbumTitle] = m_track.value("album").toString().toNSString();
    info[MPMediaItemPropertyPlaybackDuration] = @(m_audio->duration() / 1000.0);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(m_audio->position() / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(m_audio->isPlaying() ? 1.0 : 0.0);

    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = info;
    center.playbackState = m_audio->isPlaying() ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}
