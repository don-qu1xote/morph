#ifndef FAKE_HTTP_SERVER_H
#define FAKE_HTTP_SERVER_H

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <functional>
#include <memory>

// Локальный HTTP вместо api.music.yandex.net и api-v2.soundcloud.com: сервисы
// смотрят в MORPH_YANDEX_API / MORPH_SOUNDCLOUD_API. Отвечает заготовками
// по методу и пути и запоминает всё, что к нему пришло.

struct HttpRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;

    QUrlQuery form() const { return QUrlQuery(QString::fromUtf8(body)); }
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

struct HttpResponse {
    int status = 200;
    QByteArray body = "{}";
};

inline HttpResponse jsonResult(const QJsonValue& result) {
    return {200, QJsonDocument(QJsonObject{{"result", result}}).toJson(QJsonDocument::Compact)};
}

inline HttpResponse jsonBody(const QJsonObject& body) {
    return {200, QJsonDocument(body).toJson(QJsonDocument::Compact)};
}

class FakeHttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    FakeHttpServer() {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this]() { accept(); });
        m_server.listen(QHostAddress::LocalHost);
    }

    QString url() const { return QString("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    void on(const QByteArray& method, const QString& path, Handler handler) {
        m_handlers[QString::fromLatin1(method) + " " + path] = handler;
    }

    QList<HttpRequest> requestsTo(const QString& path) const {
        QList<HttpRequest> found;
        for (const HttpRequest& r : requests) {
            if (r.path == path) found.append(r);
        }
        return found;
    }

    // ждём, пока на путь придёт count запросов, крутя цикл событий
    bool waitFor(const QString& path, int count = 1, int timeoutMs = 5000) const {
        QDeadlineTimer deadline(timeoutMs);
        while (requestsTo(path).size() < count && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return requestsTo(path).size() >= count;
    }

    QList<HttpRequest> requests;

private:
    void accept() {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                handle(socket, *buffer);
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void handle(QTcpSocket* socket, const QByteArray& raw) {
        int headerEnd = raw.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        QList<QByteArray> lines = raw.left(headerEnd).split('\n');
        QList<QByteArray> requestLine = lines.takeFirst().trimmed().split(' ');
        if (requestLine.size() < 2) return;

        HttpRequest request;
        request.method = requestLine[0];
        QUrl target(QString::fromUtf8(requestLine[1]));
        request.path = target.path();
        request.query = QUrlQuery(target.query());
        for (const QByteArray& line : lines) {
            int colon = line.indexOf(':');
            if (colon > 0) request.headers[line.left(colon).trimmed().toLower()] = line.mid(colon + 1).trimmed();
        }

        int length = request.headers.value("content-length", "0").toInt();
        if (raw.size() < headerEnd + 4 + length) return;
        request.body = raw.mid(headerEnd + 4, length);
        requests.append(request);

        HttpResponse response{404, "{}"};
        auto it = m_handlers.find(QString::fromLatin1(request.method) + " " + request.path);
        if (it != m_handlers.end()) response = it.value()(request);

        QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n" +
                           "Content-Type: application/json\r\n" +
                           "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n" +
                           "Connection: close\r\n\r\n" + response.body;
        socket->write(reply);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, Handler> m_handlers;
};

#endif
