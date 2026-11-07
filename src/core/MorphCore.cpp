#include "MorphCore.h"
#include "Library.h"
#include "ServiceManager.h"
#include "../network/NetworkManager.h"
#include "../server/PhoneServer.h"
#include "../settings/SettingsManager.h"
#include "../utils/CacheManager.h"
#include "../utils/PathProvider.h"
#include <QTimer>

MorphCore::MorphCore(QObject* parent) : QObject(parent) {
    PathProvider::instance().ensureConfigExists();
    m_net = new NetworkManager(this);
    m_cache = new CacheManager(m_net, this);
    m_settings = new SettingsManager(m_cache, this);
    m_services = new ServiceManager(m_net, m_cache, this);
    SettingsManager* settings = m_settings;
    ServiceManager* services = m_services;

    // окно отдаёт токены и само при загрузке QML, а без окна больше некому
    QString yandexToken = settings->getYandexToken();
    if (!yandexToken.isEmpty()) services->setYandexToken(yandexToken);
    QString soundcloudToken = settings->getSoundCloudToken();
    if (!soundcloudToken.isEmpty()) services->setSoundCloudClientId(soundcloudToken);
    services->setYandexQuality(settings->getYandexQuality());
    services->setSoundCloudQuality(settings->getSoundCloudQuality());
    auto applyWaveSettings = [settings, services]() {
        services->setMixedWave(settings->getMixedWaveEnabled());
        services->setSyncToYandex(settings->getSyncToYandex());
        services->setSyncToSoundCloud(settings->getSyncToSoundCloud());
        services->setSyncPlaylists(settings->getSyncPlaylistsToYandex());
        services->setPinterest(settings->getPinterestToken(), settings->getPinterestBoard());
        services->setLastFmKey(settings->getLastFmKey());
        services->setSmartWaves(settings->getSmartWavesEnabled());
        // morph:new и подобные свои, в сессию Яндекса они не уходят
        QStringList seeds;
        QString discovery;
        for (const QVariant& value : settings->getWaveSettings()) {
            QString seed = value.toString();
            if (seed.startsWith("morph:")) discovery = seed.mid(6);
            else seeds.append(seed);
        }
        services->setWaveSettings(seeds);
        services->setDiscovery(discovery);
    };
    applyWaveSettings();
    connect(settings, &SettingsManager::settingsChanged, services, applyWaveSettings);

    connect(settings, &SettingsManager::likeToggled, services, &ServiceManager::setLiked);
    connect(services, &ServiceManager::likesFetched, settings, &SettingsManager::mergeServiceLikes);
    connect(services, &ServiceManager::likesImported, settings, &SettingsManager::importServiceLikes);
    // плейлисты в Яндекс: правят их по одному треку, поэтому сверка после паузы
    QTimer* playlistsTimer = new QTimer(this);
    playlistsTimer->setSingleShot(true);
    playlistsTimer->setInterval(3000);
    connect(playlistsTimer, &QTimer::timeout, services,
            [settings, services]() { services->syncPlaylists(settings->getPlaylists()); });
    connect(settings, &SettingsManager::playlistsChanged, playlistsTimer, qOverload<>(&QTimer::start));
    connect(settings, &SettingsManager::playlistRenamed, services, &ServiceManager::playlistRenamed);
    playlistsTimer->start();
    // Доска Pinterest загрузилась: плейлистам без обложки и с обложкой с доски
    // каждый запуск новая картинка. Обложку, поставленную руками, не трогаем
    connect(services, &ServiceManager::coversReady, settings, [settings, services]() {
        const QVariantMap playlists = settings->getPlaylists();
        for (auto it = playlists.cbegin(); it != playlists.cend(); ++it) {
            QString cover = it.value().toMap()["coverUrl"].toString();
            if (!cover.isEmpty() && !services->isPinterestCover(cover)) continue;
            QString fresh = services->newCover("playlist:" + it.key());
            if (!fresh.isEmpty()) settings->renamePlaylist(it.key(), it.key(), fresh);
        }
    });
    // Лайки YouTube на старте и когда в настройках выбрали другой браузер.
    // Браузеры там переключаются кликами по кругу, поэтому ждём, пока выбор устоится
    QTimer* youtubeLikesTimer = new QTimer(this);
    youtubeLikesTimer->setSingleShot(true);
    youtubeLikesTimer->setInterval(3000);
    connect(youtubeLikesTimer, &QTimer::timeout, this, [this]() {
        QString browser = m_settings->getYouTubeLikesBrowser();
        if (browser == m_youtubeLikesBrowser) return;
        m_youtubeLikesBrowser = browser;
        m_services->importYouTubeLikes(browser);
    });
    youtubeLikesTimer->start();
    connect(settings, &SettingsManager::settingsChanged, youtubeLikesTimer, qOverload<>(&QTimer::start));
    // MAIN собирается из лайков, поэтому идёт за ними следом
    Library* library = services->library();
    library->syncLikes(settings->getLikedTracks());
    connect(settings, &SettingsManager::likesChanged, library,
            [settings, library]() { library->syncLikes(settings->getLikedTracks()); });

    m_phone = new PhoneServer(settings, services, m_cache, m_net, this);
}
