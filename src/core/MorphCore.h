#ifndef MORPH_CORE_H
#define MORPH_CORE_H

#include <QObject>
#include <QString>

class CacheManager;
class NetworkManager;
class PhoneServer;
class ServiceManager;
class SettingsManager;

// morph без окна: сеть, кэш, настройки, площадки, MAIN и сервер для телефона,
// со всеми связями между ними. Окно (Application) добавляет к этому звук и QML,
// а режим --server работает на одном ядре
class MorphCore : public QObject {
    Q_OBJECT
public:
    explicit MorphCore(QObject* parent = nullptr);

    NetworkManager* net() const { return m_net; }
    CacheManager* cache() const { return m_cache; }
    SettingsManager* settings() const { return m_settings; }
    ServiceManager* services() const { return m_services; }
    PhoneServer* phone() const { return m_phone; }

private:
    NetworkManager* m_net;
    CacheManager* m_cache;
    SettingsManager* m_settings;
    ServiceManager* m_services;
    PhoneServer* m_phone;
    // из какого браузера уже импортированы лайки YouTube в этом запуске
    QString m_youtubeLikesBrowser;
};

#endif
