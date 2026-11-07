#ifndef MORPH_BACKEND_H
#define MORPH_BACKEND_H

#include "SubsonicBackend.h"
#include <QHash>
#include <QObject>

class CacheManager;
class NetworkManager;
class PhoneMixes;
class ServerAudio;
class ServiceManager;
class SettingsManager;

// Сервер для телефона поверх настоящего morph: MAIN из Library, плейлисты и
// лайки из SettingsManager, миксы из PhoneMixes, файлы из ServerAudio
class MorphBackend : public QObject, public SubsonicBackend {
    Q_OBJECT
public:
    MorphBackend(SettingsManager* settings, ServiceManager* services, CacheManager* cache, NetworkManager* net,
                 ServerAudio* audio, PhoneMixes* mixes, const QString& coverDir, QObject* parent = nullptr);

    quint64 revision() const override { return m_revision; }
    QVariantList library() const override;
    QStringList playlistNames() const override;
    QVariantList playlistTracks(const QString& name) const override;
    QString playlistCover(const QString& name) const override;
    QVariantList mixes() const override;
    bool isLiked(const QVariantMap& track) const override;
    void setLiked(const QVariantMap& track, bool liked) override;
    void listened(const QVariantMap& track, double played, double length, bool completed) override;
    QString createPlaylist(const QString& name, const QVariantList& tracks) override;
    bool updatePlaylist(const QString& name, const QString& newName, const QVariantList& tracks) override;
    void deletePlaylist(const QString& name) override;
    AudioInfo audioInfo(const QVariantMap& track) const override;
    void audio(const QVariantMap& track, AudioDone done) override;
    void cover(const QString& url, CoverDone done) override;

private:
    SettingsManager* m_settings;
    ServiceManager* m_services;
    CacheManager* m_cache;
    NetworkManager* m_net;
    ServerAudio* m_audio;
    PhoneMixes* m_mixes;
    QString m_coverDir;
    quint64 m_revision = 1;
    QHash<QString, QList<CoverDone>> m_covers;
};

#endif
