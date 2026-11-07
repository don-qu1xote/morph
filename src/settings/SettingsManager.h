#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <QObject>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickTextDocument>
#include <QTimer>
#include "../utils/CacheManager.h"
#include "../utils/QmlHighlighter.h"
#include "PlaylistManager.h"
#include "TokenManager.h"
#include "HistoryManager.h"
#include "StyleManager.h"

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(CacheManager* cache, QObject* parent = nullptr);
    ~SettingsManager() override;
    // записать отложенные правки сейчас: при выходе
    void flush();

    Q_INVOKABLE void attachHighlighter(QObject* textDocument);

public slots:
    void copyToClipboard(const QString& text);
    void saveSession(const QVariantMap& session);
    QVariantMap loadSession();

    void toggleLike(const QVariantMap& track);
    bool isLiked(const QString& trackId);
    QVariantList getLikedTracks();
    void mergeServiceLikes(const QString& service, const QVariantList& tracks, const QStringList& allIds);
    // лайки с площадки, куда писать нельзя: только добавляются, см. PlaylistManager::importLikes
    void importServiceLikes(const QString& service, const QVariantList& tracks);

    // что значат возвращаемые значения, см. PlaylistManager
    bool createPlaylist(const QString& name, const QString& coverUrl = "");
    QString createPlaylistWithTracks(const QString& name, const QString& coverUrl, const QVariantList& tracks,
                                     const QString& source = "");
    bool renamePlaylist(const QString& oldName, const QString& newName, const QString& coverUrl);
    void deletePlaylist(const QString& name);
    bool addToPlaylist(const QString& playlistName, const QVariantMap& track);
    void removeFromPlaylist(const QString& playlistName, const QString& trackId);
    bool setPlaylistTracks(const QString& playlistName, const QVariantList& tracks);
    QVariantMap getPlaylists();
    // Лёгкие запросы для интерфейса. getPlaylists отдаёт все плейлисты со всеми
    // треками, и перегон этого в JS на каждом обновлении подвешивал окно
    QVariantList playlistSummaries() const;
    QStringList playlistNames() const;
    bool hasPlaylist(const QString& name) const;
    QVariantList playlistTracks(const QString& name) const;
    QString playlistCover(const QString& name) const;
    QString uniquePlaylistName(const QString& name) const;
    QString defaultPlaylistName() const;
    QStringList playlistsWith(const QVariantMap& track) const;

    void setYandexToken(const QString& token);
    QString getYandexToken();
    void setSoundCloudToken(const QString& token);
    QString getSoundCloudToken();
    // обложки плейлистов с доски Pinterest: токен из кабинета разработчика и доска
    void setPinterestToken(const QString& token);
    QString getPinterestToken();
    void setPinterestBoard(const QString& board);
    QString getPinterestBoard();

    void addSearchHistory(const QVariantMap& track);
    QVariantList getSearchHistory();
    void clearSearchHistory();

    void addVibeHistory(const QString& trackId);
    bool isInVibeHistory(const QString& trackId);
    void clearVibeHistory();

    void setAudioQuality(const QString& quality);
    QString getAudioQuality();
    void setYandexQuality(const QString& quality);
    QString getYandexQuality();
    void setSoundCloudQuality(const QString& quality);
    QString getSoundCloudQuality();

    void setCacheLimit(qint64 bytes);
    qint64 getCacheLimit();

    void setSaveTrackCache(bool enable);
    bool getSaveTrackCache();
    void setSaveCoverCache(bool enable);
    bool getSaveCoverCache();

    void setDiscordRpcEnabled(bool enabled);
    bool getDiscordRpcEnabled();
    void setMixedWaveEnabled(bool enabled);
    bool getMixedWaveEnabled();
    // когда список кончился, играть волну по последнему треку
    void setAutoplayEnabled(bool enabled);
    bool getAutoplayEnabled();
    // порядок волн решает своя модель, а не просто порядок Яндекса
    void setSmartWavesEnabled(bool enabled);
    bool getSmartWavesEnabled();
    // лайки и прослушивания треков с SoundCloud и YouTube дублировать в Яндекс,
    // чтобы его рекомендации учитывали и их. Меняет коллекцию, поэтому выключено
    void setSyncToYandex(bool enabled);
    bool getSyncToYandex();
    // песни MAIN лайками в аккаунт SoundCloud, чтобы его рекомендации учились на них
    void setSyncToSoundCloud(bool enabled);
    bool getSyncToSoundCloud();
    // плейлисты morph настоящими плейлистами в Яндексе
    void setSyncPlaylistsToYandex(bool enabled);
    bool getSyncPlaylistsToYandex();
    // из какого браузера yt-dlp читает cookies для лайков YouTube; пусто, если выключено
    void setYouTubeLikesBrowser(const QString& browser);
    QString getYouTubeLikesBrowser();
    // настройки Моей волны: ключ (diversity, moodEnergy, language) и сид значения
    void setWaveSetting(const QString& key, const QString& seed);
    QVariantMap getWaveSettings();

    Q_INVOKABLE QVariantMap getAboutInfo();

    Q_INVOKABLE QString getStyleFileContent();
    Q_INVOKABLE bool writeStyleFileContent(const QString& content);
    Q_INVOKABLE QString getStyleContentByName(const QString& fileName);
    Q_INVOKABLE bool writeStyleContentByName(const QString& fileName, const QString& content);
    Q_INVOKABLE void saveTemporaryPreview(const QString& content);
    Q_INVOKABLE bool importStyleFile(const QString& fileUrl);
    Q_INVOKABLE bool exportStyleFile(const QString& fileName, const QString& fileUrl);
    Q_INVOKABLE bool deleteStyleFile(const QString& fileName);

    Q_INVOKABLE QString getStylePreview(const QString& fileName);    void saveStylePreview(const QString& fileName, const QString& base64);
    Q_INVOKABLE bool savePreviewFromClipboard(const QString& fileName);
    Q_INVOKABLE QString getStyleColors(const QString& fileName);
    Q_INVOKABLE QString getStyleConfigVersion(const QString& fileName);
    Q_INVOKABLE QString getStyleAppVersion(const QString& fileName);

    Q_INVOKABLE QVariantList getStyleFileList();
    Q_INVOKABLE QString getActiveStyleName();
    Q_INVOKABLE void setActiveStyleName(const QString& name);
    QString getActiveStylePath();

signals:
    void likesChanged();
    void likeToggled(const QVariantMap& track, bool liked);
    void playlistsChanged();
    void playlistRenamed(const QString& oldName, const QString& newName);
    void settingsChanged();

private:
    QString m_path;
    QJsonObject m_data;
    CacheManager* cache;
    PlaylistManager* m_playlists;
    TokenManager* m_tokens;
    HistoryManager* m_history;
    StyleManager* m_styles;
    // правки за пару секунд складываются в одну запись, она идёт в фоне
    QTimer m_saveTimer;
    bool m_dirty = false;
    void load();
    void save();
};

#endif
