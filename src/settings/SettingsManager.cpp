#include "SettingsManager.h"
#include "../utils/BackgroundWriter.h"
#include "../utils/PathProvider.h"
#include <QDir>
#include <QSysInfo>

SettingsManager::SettingsManager(CacheManager* cache, QObject* parent) : QObject(parent), cache(cache) {
    m_path = PathProvider::getDataPath() + "/settings.json";
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsManager::flush);
    load();

    m_playlists = new PlaylistManager(&m_data, this);
    m_tokens = new TokenManager(&m_data, this);
    m_history = new HistoryManager(&m_data, this);
    m_styles = new StyleManager(&m_data, this);

    connect(m_playlists, &PlaylistManager::likesChanged, this, &SettingsManager::likesChanged);
    connect(m_playlists, &PlaylistManager::playlistsChanged, this, &SettingsManager::playlistsChanged);
    connect(m_tokens, &TokenManager::settingsChanged, this, &SettingsManager::settingsChanged);
    connect(m_styles, &StyleManager::settingsChanged, this, &SettingsManager::settingsChanged);
}

void SettingsManager::attachHighlighter(QObject* textDocument) {
    if (!textDocument) return;
    QQuickTextDocument* doc = qobject_cast<QQuickTextDocument*>(textDocument);
    if (doc) {
        new QmlHighlighter(doc->textDocument());
    }
}

void SettingsManager::copyToClipboard(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
}

void SettingsManager::load() {
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        m_data = QJsonDocument::fromJson(file.readAll()).object();
        file.close();

        if (m_data.contains("cache_limit")) {
            cache->setLimit(m_data["cache_limit"].toVariant().toLongLong());
        }

        if (m_data.contains("save_track_cache")) {
            cache->setSaveTracks(m_data["save_track_cache"].toBool());
        }
        if (m_data.contains("save_cover_cache")) {
            cache->setSaveCovers(m_data["save_cover_cache"].toBool());
        }

        if (m_data.contains("playlists") && m_data["playlists"].isObject()) {
            QJsonObject playlists = m_data["playlists"].toObject();
            bool changed = false;
            for (auto it = playlists.begin(); it != playlists.end(); ++it) {
                if (it.value().isArray()) {
                    QJsonObject newPl;
                    newPl["tracks"] = it.value().toArray();
                    newPl["coverUrl"] = "";
                    playlists[it.key()] = newPl;
                    changed = true;
                }
            }
            if (changed) {
                m_data["playlists"] = playlists;
                save();
            }
        }
    }
}

SettingsManager::~SettingsManager() {
    flush();
    BackgroundWriter::waitForAll();
}

// В settings.json лежат все лайки и плейлисты, это мегабайты. Раньше он
// целиком переписывался прямо в окне на каждый лайк и каждую смену трека
void SettingsManager::save() {
    m_dirty = true;
    if (!m_saveTimer.isActive()) m_saveTimer.start();
}

void SettingsManager::flush() {
    m_saveTimer.stop();
    if (!m_dirty) return;
    m_dirty = false;
    BackgroundWriter::write(m_path, m_data);
}

void SettingsManager::saveSession(const QVariantMap& session) {
    m_data["session"] = QJsonObject::fromVariantMap(session);
    save();
}

QVariantMap SettingsManager::loadSession() {
    return m_data["session"].toObject().toVariantMap();
}

void SettingsManager::toggleLike(const QVariantMap& track) {
    QVariantList removed = m_playlists->toggleLike(track);
    save();
    if (removed.isEmpty()) emit likeToggled(track, true);
    // каждая снятая версия уходит в свой сервис
    for (const QVariant& t : std::as_const(removed)) emit likeToggled(t.toMap(), false);
}

void SettingsManager::mergeServiceLikes(const QString& service, const QVariantList& tracks, const QStringList& allIds) {
    m_playlists->mergeServiceLikes(service, tracks, allIds);
    save();
}

void SettingsManager::importServiceLikes(const QString& service, const QVariantList& tracks) {
    // первый импорт это старые лайки, им место в конце; дальше новые идут наверх
    QString flag = "imported_likes_" + service.toLower();
    int added = m_playlists->importLikes(tracks, m_data[flag].toBool());
    m_data[flag] = true;
    if (added > 0) save();
}

bool SettingsManager::isLiked(const QString& trackId) {
    return m_playlists->isLiked(trackId);
}

QVariantList SettingsManager::getLikedTracks() {
    return m_playlists->getLikedTracks();
}

bool SettingsManager::createPlaylist(const QString& name, const QString& coverUrl) {
    if (!m_playlists->createPlaylist(name, coverUrl)) return false;
    save();
    return true;
}

QString SettingsManager::createPlaylistWithTracks(const QString& name, const QString& coverUrl,
                                                  const QVariantList& tracks, const QString& source) {
    QString saved = m_playlists->createPlaylistWithTracks(name, coverUrl, tracks, source);
    save();
    return saved;
}

bool SettingsManager::renamePlaylist(const QString& oldName, const QString& newName, const QString& coverUrl) {
    if (!m_playlists->renamePlaylist(oldName, newName, coverUrl)) return false;
    save();
    if (oldName != newName.trimmed()) emit playlistRenamed(oldName, newName.trimmed());
    return true;
}

void SettingsManager::deletePlaylist(const QString& name) {
    m_playlists->deletePlaylist(name);
    save();
}

bool SettingsManager::addToPlaylist(const QString& playlistName, const QVariantMap& track) {
    if (!m_playlists->addToPlaylist(playlistName, track)) return false;
    save();
    return true;
}

QString SettingsManager::uniquePlaylistName(const QString& name) const {
    return m_playlists->uniqueName(name);
}

QString SettingsManager::defaultPlaylistName() const {
    return m_playlists->defaultName();
}

QStringList SettingsManager::playlistsWith(const QVariantMap& track) const {
    return m_playlists->playlistsWith(track);
}

void SettingsManager::removeFromPlaylist(const QString& playlistName, const QString& trackId) {
    m_playlists->removeFromPlaylist(playlistName, trackId);
    save();
}

bool SettingsManager::setPlaylistTracks(const QString& playlistName, const QVariantList& tracks) {
    if (!m_playlists->setPlaylistTracks(playlistName, tracks)) return false;
    save();
    return true;
}

QVariantMap SettingsManager::getPlaylists() {
    return m_playlists->getPlaylists();
}

void SettingsManager::setYandexToken(const QString& token) {
    m_tokens->setYandexToken(token);
    save();
}

QString SettingsManager::getYandexToken() {
    return m_tokens->getYandexToken();
}

void SettingsManager::setSoundCloudToken(const QString& token) {
    m_tokens->setSoundCloudToken(token);
    save();
}

QString SettingsManager::getSoundCloudToken() {
    return m_tokens->getSoundCloudToken();
}

void SettingsManager::setPinterestToken(const QString& token) {
    m_tokens->setPinterestToken(token);
    save();
}

QString SettingsManager::getPinterestToken() {
    return m_tokens->getPinterestToken();
}

void SettingsManager::setPinterestBoard(const QString& board) {
    m_data["pinterest_board"] = board.trimmed();
    save();
    emit settingsChanged();
}

QString SettingsManager::getPinterestBoard() {
    return m_data["pinterest_board"].toString();
}

void SettingsManager::addSearchHistory(const QVariantMap& track) {
    m_history->addSearchHistory(track);
    save();
}

QVariantList SettingsManager::getSearchHistory() {
    return m_history->getSearchHistory();
}

void SettingsManager::clearSearchHistory() {
    m_data.remove("history");
    save();
    emit settingsChanged();
}

void SettingsManager::addVibeHistory(const QString& trackId) {
    m_history->addVibeHistory(trackId);
    save();
}

bool SettingsManager::isInVibeHistory(const QString& trackId) {
    return m_history->isInVibeHistory(trackId);
}

void SettingsManager::clearVibeHistory() {
    m_history->clearVibeHistory();
    save();
}

void SettingsManager::setAudioQuality(const QString& quality) {
    setYandexQuality(quality);
}

QString SettingsManager::getAudioQuality() {
    return getYandexQuality();
}

void SettingsManager::setYandexQuality(const QString& quality) {
    m_data["yandex_quality"] = quality;
    save();
    emit settingsChanged();
}

QString SettingsManager::getYandexQuality() {
    if (m_data.contains("yandex_quality")) {
        return m_data["yandex_quality"].toString();
    }
    if (m_data.contains("audio_quality") && m_data["audio_quality"].toString() == "low") {
        return "192";
    }
    return "320";
}

void SettingsManager::setSoundCloudQuality(const QString& quality) {
    m_data["soundcloud_quality"] = quality;
    save();
    emit settingsChanged();
}

QString SettingsManager::getSoundCloudQuality() {
    if (m_data.contains("soundcloud_quality")) {
        return m_data["soundcloud_quality"].toString();
    }
    return "128";
}

void SettingsManager::setCacheLimit(qint64 bytes) {
    m_data["cache_limit"] = QString::number(bytes);
    cache->setLimit(bytes);
    save();
    emit settingsChanged();
}

qint64 SettingsManager::getCacheLimit() {
    if (!m_data.contains("cache_limit")) return 0;
    return m_data["cache_limit"].toVariant().toLongLong();
}

void SettingsManager::setSaveTrackCache(bool enable) {
    m_data["save_track_cache"] = enable;
    cache->setSaveTracks(enable);
    this->save();
    emit settingsChanged();
}

bool SettingsManager::getSaveTrackCache() {
    if (!m_data.contains("save_track_cache")) return true;
    return m_data["save_track_cache"].toBool();
}

void SettingsManager::setSaveCoverCache(bool enable) {
    m_data["save_cover_cache"] = enable;
    cache->setSaveCovers(enable);
    this->save();
    emit settingsChanged();
}

bool SettingsManager::getSaveCoverCache() {
    if (!m_data.contains("save_cover_cache")) return true;
    return m_data["save_cover_cache"].toBool();
}

void SettingsManager::setDiscordRpcEnabled(bool enabled) {
    m_data["discord_rpc"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getDiscordRpcEnabled() {
    if (!m_data.contains("discord_rpc")) return true;
    return m_data["discord_rpc"].toBool();
}

void SettingsManager::setMixedWaveEnabled(bool enabled) {
    m_data["mixed_wave"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getMixedWaveEnabled() {
    if (!m_data.contains("mixed_wave")) return true;
    return m_data["mixed_wave"].toBool();
}

void SettingsManager::setAutoplayEnabled(bool enabled) {
    m_data["autoplay"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getAutoplayEnabled() {
    if (!m_data.contains("autoplay")) return true;
    return m_data["autoplay"].toBool();
}

void SettingsManager::setSmartWavesEnabled(bool enabled) {
    m_data["smart_waves"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getSmartWavesEnabled() {
    return m_data["smart_waves"].toBool(true);
}

void SettingsManager::setYouTubeLikesBrowser(const QString& browser) {
    m_data["youtube_likes_browser"] = browser;
    save();
    emit settingsChanged();
}

QString SettingsManager::getYouTubeLikesBrowser() {
    return m_data["youtube_likes_browser"].toString();
}

void SettingsManager::setSyncPlaylistsToYandex(bool enabled) {
    m_data["sync_playlists_to_yandex"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getSyncPlaylistsToYandex() {
    return m_data["sync_playlists_to_yandex"].toBool(false);
}

void SettingsManager::setSyncToSoundCloud(bool enabled) {
    m_data["sync_to_soundcloud"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getSyncToSoundCloud() {
    return m_data["sync_to_soundcloud"].toBool(false);
}

void SettingsManager::setSyncToYandex(bool enabled) {
    m_data["sync_to_yandex"] = enabled;
    save();
    emit settingsChanged();
}

bool SettingsManager::getSyncToYandex() {
    return m_data["sync_to_yandex"].toBool(false);
}

void SettingsManager::setWaveSetting(const QString& key, const QString& seed) {
    QVariantMap wave = getWaveSettings();
    if (seed.isEmpty()) wave.remove(key);
    else wave[key] = seed;
    m_data["wave_settings"] = QJsonObject::fromVariantMap(wave);
    save();
    emit settingsChanged();
}

QVariantMap SettingsManager::getWaveSettings() {
    return m_data["wave_settings"].toObject().toVariantMap();
}

QVariantMap SettingsManager::getAboutInfo() {
    QVariantMap info;
#ifdef MORPH_VERSION
    info["version"] = QString(MORPH_VERSION);
#else
    info["version"] = "unknown";
#endif
    info["build_date"] = QString(__DATE__);
    info["build_time"] = QString(__TIME__);
    info["qt_version"] = QString(QT_VERSION_STR);
    info["os_name"] = QSysInfo::prettyProductName();
    info["os_version"] = QSysInfo::productVersion();
    info["kernel"] = QSysInfo::kernelVersion();
    info["arch"] = QSysInfo::currentCpuArchitecture();

#ifdef MORPH_BUILD_NUMBER
    info["build_number"] = QString(MORPH_BUILD_NUMBER);
#else
    info["build_number"] = "unknown";
#endif

    return info;
}

QString SettingsManager::getStyleFileContent() {
    return m_styles->getStyleFileContent();
}

bool SettingsManager::writeStyleFileContent(const QString& content) {
    return m_styles->writeStyleFileContent(content);
}

QString SettingsManager::getStyleContentByName(const QString& fileName) {
    return m_styles->getStyleContentByName(fileName);
}

bool SettingsManager::writeStyleContentByName(const QString& fileName, const QString& content) {
    return m_styles->writeStyleContentByName(fileName, content);
}

void SettingsManager::saveTemporaryPreview(const QString& content) {
    m_styles->saveTemporaryPreview(content);
}

bool SettingsManager::importStyleFile(const QString& fileUrl) {
    return m_styles->importStyleFile(fileUrl);
}

bool SettingsManager::exportStyleFile(const QString& fileName, const QString& fileUrl) {
    return m_styles->exportStyleFile(fileName, fileUrl);
}

bool SettingsManager::deleteStyleFile(const QString& fileName) {
    return m_styles->deleteStyleFile(fileName);
}

QString SettingsManager::getStylePreview(const QString& fileName) {
    return m_styles->getStylePreview(fileName);
}

void SettingsManager::saveStylePreview(const QString& fileName, const QString& base64) {
    m_styles->saveStylePreview(fileName, base64);
}

bool SettingsManager::savePreviewFromClipboard(const QString& fileName) {
    return m_styles->savePreviewFromClipboard(fileName);
}

QString SettingsManager::getStyleColors(const QString& fileName) {
    return m_styles->getStyleColors(fileName);
}

QString SettingsManager::getStyleConfigVersion(const QString& fileName) {
    return m_styles->getStyleConfigVersion(fileName);
}

QString SettingsManager::getStyleAppVersion(const QString& fileName) {
    return m_styles->getStyleAppVersion(fileName);
}

QVariantList SettingsManager::getStyleFileList() {
    return m_styles->getStyleFileList();
}

QString SettingsManager::getActiveStyleName() {
    return m_styles->getActiveStyleName();
}

void SettingsManager::setActiveStyleName(const QString& name) {
    m_styles->setActiveStyleName(name);
    save();
}

QString SettingsManager::getActiveStylePath() {
    return m_styles->getActiveStylePath();
}

QVariantList SettingsManager::playlistSummaries() const {
    QVariantList out;
    const QJsonObject playlists = m_data["playlists"].toObject();
    for (auto it = playlists.begin(); it != playlists.end(); ++it) {
        QJsonObject p = it.value().toObject();
        out.append(QVariantMap{
            {"name", it.key()}, {"coverUrl", p["coverUrl"].toString()}, {"trackCount", p["tracks"].toArray().size()}});
    }
    return out;
}

QStringList SettingsManager::playlistNames() const {
    return m_data["playlists"].toObject().keys();
}

bool SettingsManager::hasPlaylist(const QString& name) const {
    return !name.isEmpty() && m_data["playlists"].toObject().contains(name);
}

QVariantList SettingsManager::playlistTracks(const QString& name) const {
    return m_data["playlists"].toObject()[name].toObject()["tracks"].toArray().toVariantList();
}

QString SettingsManager::playlistCover(const QString& name) const {
    return m_data["playlists"].toObject()[name].toObject()["coverUrl"].toString();
}
