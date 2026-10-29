#ifndef MPRIS_MANAGER_H
#define MPRIS_MANAGER_H

#include <QObject>
#include <QVariantMap>

class AudioEngine;

#ifdef Q_OS_MACOS
class MacNowPlaying;
#else
#include "MprisRootAdaptor.h"
#include "MprisPlayerAdaptor.h"
#endif

// Имя осталось от MPRIS, потому что в QML он везде MorphMpris.
// На маке вместо DBus тут системный "Сейчас играет" и медиаклавиши.
class MprisManager : public QObject {
    Q_OBJECT
public:
    explicit MprisManager(AudioEngine* audio, QObject* parent = nullptr);
    QString currentTitle() const;
    QString currentArtist() const;

public slots:
    void updateMetadata(const QVariantMap& track);

signals:
    void nextRequested();
    void previousRequested();
    void metadataChanged();

private:
#ifdef Q_OS_MACOS
    MacNowPlaying* m_nowPlaying;
    QVariantMap m_track;
#else
    MprisRootAdaptor* m_root;
    MprisPlayerAdaptor* m_player;
#endif
};

#endif
