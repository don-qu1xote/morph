#ifndef MAC_NOW_PLAYING_H
#define MAC_NOW_PLAYING_H

#include <QObject>
#include <QVariantMap>

class AudioEngine;

// Без регистрации в MPRemoteCommandCenter F7-F9 и кнопки на наушниках
// уходят в Apple Music, а не сюда
class MacNowPlaying : public QObject {
    Q_OBJECT
public:
    explicit MacNowPlaying(AudioEngine* audio, QObject* parent = nullptr);
    ~MacNowPlaying() override;

    void setTrack(const QVariantMap& track);

signals:
    void nextRequested();
    void previousRequested();

private:
    void publish();

    AudioEngine* m_audio;
    QVariantMap m_track;
};

#endif
