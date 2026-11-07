#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QString>

class TokenManager : public QObject {
    Q_OBJECT
public:
    explicit TokenManager(QJsonObject* data, QObject* parent = nullptr);

    void setYandexToken(const QString& token);
    QString getYandexToken();
    void setSoundCloudToken(const QString& token);
    QString getSoundCloudToken();
    void setPinterestToken(const QString& token);
    QString getPinterestToken();
    // пароль сервера для телефона: Subsonic считает от него хэш, поэтому
    // хранится обратимо, как и токены
    void setPhonePassword(const QString& password);
    QString getPhonePassword();

signals:
    void settingsChanged();

private:
    QJsonObject* m_data;
};

#endif
