#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>

class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject* parent = nullptr);
    QNetworkAccessManager* getNetworkAccessManager() const { return manager; }
    // showErrors = false для фоновых запросов (статистика, фидбек волны),
    // иначе каждый отвалившийся запрос всплывает тостом в интерфейсе
    void get(const QUrl& url, const QMap<QString, QByteArray>& headers, std::function<void(QNetworkReply*)> callback,
             bool showErrors = true);
    void rawGet(const QUrl& url, const QMap<QString, QByteArray>& headers, std::function<void(QNetworkReply*)> callback);
    void post(const QUrl& url, const QByteArray& data, const QMap<QString, QByteArray>& headers,
              std::function<void(QNetworkReply*)> callback, bool showErrors = true);

signals:
    void errorOccurred(const QString& message);

private:
    QNetworkAccessManager* manager;
};

#endif
