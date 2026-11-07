#include "PhoneServer.h"
#include "MorphBackend.h"
#include "PhoneMixes.h"
#include "ServerAudio.h"
#include "SubsonicApi.h"
#include "../core/MorphMixes.h"
#include "../core/ServiceManager.h"
#include "../settings/SettingsManager.h"
#include "../utils/PathProvider.h"
#include <QDateTime>
#include <QNetworkInterface>
#include <QRandomGenerator>

// клиент дёргает сервер сотнями запросов за синхронизацию: окну хватит раза в полминуты
static const qint64 kSeenNotifyMs = 30000;

PhoneServer::PhoneServer(SettingsManager* settings, ServiceManager* services, CacheManager* cache, NetworkManager* net,
                         QObject* parent)
    : QObject(parent), m_settings(settings) {
    QString dir = PathProvider::getDataPath();
    m_audio = new ServerAudio(services, cache, dir + "/phone_audio", this);

    MorphMixes* source = services->phoneMixes();
    m_mixes = new PhoneMixes([source] { return source->cards(); }, [source](const QString& id) { source->build(id); },
                             dir + "/phone_mixes.json", this);
    connect(source, &MorphMixes::ready, m_mixes, &PhoneMixes::collected);
    connect(source, &MorphMixes::failed, m_mixes, &PhoneMixes::skipped);
    // треки новых миксов сразу на мак: телефон заберёт их без ожидания
    connect(m_mixes, &PhoneMixes::built, m_audio, &ServerAudio::prefetch);
    connect(m_mixes, &PhoneMixes::changed, this, &PhoneServer::mixesChanged);

    m_backend = new MorphBackend(settings, services, cache, net, m_audio, m_mixes, dir + "/phone_covers", this);
    m_api = new SubsonicApi(m_backend, dir, this);
    m_http.setHandler([this](const IncomingRequest& request, std::shared_ptr<HttpResponder> out) {
        m_api->handle(request, std::move(out));
    });
    connect(m_api, &SubsonicApi::clientSeen, this, [this](const QString& client) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QString name = client.isEmpty() ? QStringLiteral("Subsonic app") : client;
        bool notify = name != m_lastClient || now - m_lastSeen > kSeenNotifyMs;
        m_lastClient = name;
        if (!notify) return;
        m_lastSeen = now;
        emit clientChanged();
    });
}

void PhoneServer::start() {
    if (!m_settings->getPhoneServerEnabled()) return;
    if (password().isEmpty()) newPassword();
    if (!running()) listen();
    m_mixes->setEnabled(true);
}

bool PhoneServer::listen() {
    m_api->setCredentials(user(), password());
    bool ok = m_http.listen(QHostAddress::AnyIPv4, kPort);
    m_error = ok ? QString() : QString("Port %1 is busy. Is another music server running?").arg(kPort);
    emit stateChanged();
    return ok;
}

void PhoneServer::stop() {
    m_http.close();
    m_mixes->setEnabled(false);
    emit stateChanged();
}

void PhoneServer::setEnabled(bool enabled) {
    m_settings->setPhoneServerEnabled(enabled);
    if (enabled) start();
    else stop();
}

QString PhoneServer::password() const {
    return m_settings->getPhonePassword();
}

// без похожих 0 и O, 1 и l: пароль вводят руками на телефоне
void PhoneServer::newPassword() {
    static const QString alphabet = QStringLiteral("abcdefghijkmnpqrstuvwxyz23456789");
    QString password;
    for (int i = 0; i < 10; ++i) password += alphabet[QRandomGenerator::system()->bounded(int(alphabet.size()))];
    m_settings->setPhonePassword(password);
    m_api->setCredentials(user(), password);
    emit stateChanged();
}

int PhoneServer::mixCount() const {
    return int(m_mixes->mixes().size());
}

static bool isPrivate(const QHostAddress& ip) {
    quint32 a = ip.toIPv4Address();
    return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1) || (a >> 16) == ((192u << 8) | 168);
}

// Адрес в домашней сети. VPN-туннели (utun, ppp, ipsec) не годятся: телефон
// ходит к маку через Wi-Fi, а не через туннель
QString PhoneServer::address() const {
    QString fallback;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& i : interfaces) {
        auto flags = i.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        QString name = i.name();
        if (name.startsWith("utun") || name.startsWith("ppp") || name.startsWith("ipsec") || name.startsWith("tun")) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : i.addressEntries()) {
            QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) continue;
            QString url = QString("http://%1:%2").arg(ip.toString()).arg(kPort);
            if (isPrivate(ip)) return url;
            if (fallback.isEmpty()) fallback = url;
        }
    }
    return fallback;
}
