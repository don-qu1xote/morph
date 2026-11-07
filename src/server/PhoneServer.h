#ifndef PHONE_SERVER_H
#define PHONE_SERVER_H

#include "HttpServer.h"
#include <QObject>

class CacheManager;
class MorphBackend;
class NetworkManager;
class PhoneMixes;
class ServerAudio;
class ServiceManager;
class SettingsManager;
class SubsonicApi;

// Сервер для телефона целиком: HTTP, Subsonic, файлы и миксы. В QML это
// MorphPhone: включить, адрес и пароль для Amperfy, когда телефон заходил
class PhoneServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString address READ address NOTIFY stateChanged)
    Q_PROPERTY(QString user READ user CONSTANT)
    Q_PROPERTY(QString password READ password NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString lastClient READ lastClient NOTIFY clientChanged)
    Q_PROPERTY(qint64 lastSeen READ lastSeen NOTIFY clientChanged)
    Q_PROPERTY(int mixCount READ mixCount NOTIFY mixesChanged)
public:
    static const quint16 kPort = 4533;

    PhoneServer(SettingsManager* settings, ServiceManager* services, CacheManager* cache, NetworkManager* net,
                QObject* parent = nullptr);

    bool running() const { return m_http.isListening(); }
    // http://192.168.1.20:4533, как вписать в Amperfy
    QString address() const;
    QString user() const { return QStringLiteral("morph"); }
    QString password() const;
    QString error() const { return m_error; }
    QString lastClient() const { return m_lastClient; }
    qint64 lastSeen() const { return m_lastSeen; }
    int mixCount() const;

    // поднять, если сервер включён в настройках
    void start();

public slots:
    void setEnabled(bool enabled);
    void newPassword();

signals:
    void stateChanged();
    void clientChanged();
    void mixesChanged();

private:
    bool listen();
    void stop();

    SettingsManager* m_settings;
    HttpServer m_http;
    ServerAudio* m_audio;
    PhoneMixes* m_mixes;
    MorphBackend* m_backend;
    SubsonicApi* m_api;
    QString m_error;
    QString m_lastClient;
    qint64 m_lastSeen = 0;
};

#endif
