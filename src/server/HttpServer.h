#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <functional>
#include <memory>

struct IncomingRequest {
    QByteArray method;
    QString path;
    // параметры из адреса и из тела формы вместе: Subsonic принимает и так, и так
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;
    QHostAddress peer;
};

// Ответ на один запрос. Можно ответить сразу или позже, когда, например,
// докачается трек: объект живёт, пока на него есть ссылка
class HttpResponder {
public:
    explicit HttpResponder(QTcpSocket* socket, bool headOnly) : m_socket(socket), m_headOnly(headOnly) {}
    void send(int status, const QByteArray& contentType, const QByteArray& body);
    // файл целиком или кусок по заголовку Range: плеер на телефоне так перематывает
    void sendFile(const QString& path, const QByteArray& contentType, const QByteArray& range);
    bool isOpen() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState && !m_done; }

private:
    void writeHead(int status, const QList<QPair<QByteArray, QByteArray>>& headers);
    QPointer<QTcpSocket> m_socket;
    bool m_headOnly;
    bool m_done = false;
};

using HttpHandler = std::function<void(const IncomingRequest&, std::shared_ptr<HttpResponder>)>;

// Маленький HTTP/1.1 сервер на QTcpServer: GET, HEAD и POST с формой, по
// одному запросу на соединение. QtHttpServer есть не во всех сборках Qt, а
// для Subsonic больше и не нужно
class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(QObject* parent = nullptr);
    bool listen(const QHostAddress& address, quint16 port);
    void close();
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }
    void setHandler(HttpHandler handler) { m_handler = std::move(handler); }

private:
    void accept();
    void readFrom(QTcpSocket* socket);

    QTcpServer m_server;
    HttpHandler m_handler;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

#endif
