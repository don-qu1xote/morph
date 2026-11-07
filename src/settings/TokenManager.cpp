#include "TokenManager.h"
#include "../utils/CryptoUtils.h"

TokenManager::TokenManager(QJsonObject* data, QObject* parent)
    : QObject(parent), m_data(data) {}

void TokenManager::setYandexToken(const QString& token) {
    (*m_data)["yandex_token"] = CryptoUtils::encrypt(token);
    emit settingsChanged();
}

QString TokenManager::getYandexToken() {
    return CryptoUtils::decrypt((*m_data)["yandex_token"].toString());
}

void TokenManager::setSoundCloudToken(const QString& token) {
    (*m_data)["soundcloud_token"] = CryptoUtils::encrypt(token);
    emit settingsChanged();
}

QString TokenManager::getSoundCloudToken() {
    return CryptoUtils::decrypt((*m_data)["soundcloud_token"].toString());
}

void TokenManager::setPinterestToken(const QString& token) {
    (*m_data)["pinterest_token"] = CryptoUtils::encrypt(token);
    emit settingsChanged();
}

void TokenManager::setPhonePassword(const QString& password) {
    (*m_data)["phone_password"] = CryptoUtils::encrypt(password);
}

QString TokenManager::getPhonePassword() {
    return CryptoUtils::decrypt((*m_data)["phone_password"].toString());
}

void TokenManager::setLastFmKey(const QString& key) {
    (*m_data)["lastfm_key"] = CryptoUtils::encrypt(key.trimmed());
    emit settingsChanged();
}

QString TokenManager::getLastFmKey() {
    return CryptoUtils::decrypt((*m_data)["lastfm_key"].toString());
}

QString TokenManager::getPinterestToken() {
    return CryptoUtils::decrypt((*m_data)["pinterest_token"].toString());
}
