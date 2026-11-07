#include "HttpServer.h"
#include <QFile>
#include <QRegularExpression>
#include <QTimer>

// заголовки длиннее не бывают у честных клиентов
static const int kMaxHeader = 64 * 1024;
static const int kMaxBody = 1024 * 1024;
static const qint64 kChunk = 256 * 1024;
// соединение без запроса дольше этого закрывается
static const int kIdleMs = 30000;

static QByteArray reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 206:
        return "Partial Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 416:
        return "Range Not Satisfiable";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

HttpServer::HttpServer(QObject* parent) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::accept);
}

bool HttpServer::listen(const QHostAddress& address, quint16 port) {
    return m_server.listen(address, port);
}

void HttpServer::close() {
    m_server.close();
}

void HttpServer::accept() {
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        auto* idle = new QTimer(socket);
        idle->setSingleShot(true);
        connect(idle, &QTimer::timeout, socket, &QTcpSocket::abort);
        idle->start(kIdleMs);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void HttpServer::readFrom(QTcpSocket* socket) {
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end()) return;
    QByteArray& buffer = *it;
    buffer.append(socket->readAll());

    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > kMaxHeader) socket->abort();
        return;
    }

    IncomingRequest request;
    request.peer = socket->peerAddress();
    QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    QList<QByteArray> first = lines.takeFirst().trimmed().split(' ');
    if (first.size() < 2) {
        socket->abort();
        return;
    }
    request.method = first[0].toUpper();
    for (const QByteArray& line : std::as_const(lines)) {
        int colon = line.indexOf(':');
        if (colon > 0) request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    qsizetype length = request.headers.value("content-length").toLongLong();
    if (length > kMaxBody) {
        socket->abort();
        return;
    }
    if (buffer.size() < headerEnd + 4 + length) return;
    QByteArray body = buffer.mid(headerEnd + 4, length);
    // запрос прочитан целиком, остальное в этом соединении не ждём
    m_buffers.remove(socket);
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    if (auto* idle = socket->findChild<QTimer*>()) idle->stop();

    QUrl target(QString::fromUtf8(first[1]));
    request.path = target.path();
    request.query = QUrlQuery(target);
    if (request.headers.value("content-type").startsWith("application/x-www-form-urlencoded")) {
        // плюс в форме это пробел, QUrlQuery сам его так не читает
        QUrlQuery form(QString::fromUtf8(body).replace('+', ' '));
        for (const auto& item : form.queryItems(QUrl::FullyDecoded))
            request.query.addQueryItem(item.first, item.second);
    }

    auto responder = std::make_shared<HttpResponder>(socket, request.method == "HEAD");
    if (m_handler) m_handler(request, responder);
    else responder->send(404, "text/plain", "no handler");
}

void HttpResponder::writeHead(int status, const QList<QPair<QByteArray, QByteArray>>& headers) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason(status) + "\r\n";
    for (const auto& h : headers) head += h.first + ": " + h.second + "\r\n";
    head += "Connection: close\r\n\r\n";
    m_socket->write(head);
}

void HttpResponder::send(int status, const QByteArray& contentType, const QByteArray& body) {
    if (!isOpen()) return;
    m_done = true;
    writeHead(status, {{"Content-Type", contentType}, {"Content-Length", QByteArray::number(body.size())}});
    if (!m_headOnly) m_socket->write(body);
    m_socket->disconnectFromHost();
}

void HttpResponder::sendFile(const QString& path, const QByteArray& contentType, const QByteArray& range) {
    if (!isOpen()) return;
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        send(404, "text/plain", "not found");
        return;
    }
    qint64 size = file->size();
    qint64 from = 0;
    qint64 to = size - 1;
    bool partial = false;

    // bytes=100-, bytes=100-200 и bytes=-500 (последние 500)
    static const QRegularExpression rx("^bytes=(\\d*)-(\\d*)$");
    QRegularExpressionMatch m = rx.match(QString::fromLatin1(range).trimmed());
    if (m.hasMatch() && !(m.captured(1).isEmpty() && m.captured(2).isEmpty())) {
        partial = true;
        if (m.captured(1).isEmpty()) {
            from = qMax<qint64>(0, size - m.captured(2).toLongLong());
        } else {
            from = m.captured(1).toLongLong();
            if (!m.captured(2).isEmpty()) to = qMin(to, m.captured(2).toLongLong());
        }
        if (from > to || from >= size) {
            m_done = true;
            writeHead(416, {{"Content-Range", "bytes */" + QByteArray::number(size)}, {"Content-Length", "0"}});
            m_socket->disconnectFromHost();
            return;
        }
    }

    QList<QPair<QByteArray, QByteArray>> headers{{"Content-Type", contentType},
                                                 {"Accept-Ranges", "bytes"},
                                                 {"Content-Length", QByteArray::number(to - from + 1)}};
    if (partial) {
        headers.append({"Content-Range", "bytes " + QByteArray::number(from) + "-" + QByteArray::number(to) + "/" +
                                             QByteArray::number(size)});
    }
    m_done = true;
    writeHead(partial ? 206 : 200, headers);
    if (m_headOnly) {
        m_socket->disconnectFromHost();
        return;
    }

    // Файл уходит кусками по мере того, как сокет их отправляет: трек в
    // память целиком не читается, и медленный телефон не раздувает буфер
    file->seek(from);
    auto left = std::make_shared<qint64>(to - from + 1);
    QPointer<QTcpSocket> socket = m_socket;
    auto pump = [socket, file, left]() {
        if (!socket) return;
        // телефон, который перестал забирать данные, не держит соединение вечно
        if (auto* idle = socket->findChild<QTimer*>()) idle->start();
        while (*left > 0 && socket->bytesToWrite() < 2 * kChunk) {
            QByteArray chunk = file->read(qMin(kChunk, *left));
            if (chunk.isEmpty()) {
                *left = 0;
                break;
            }
            *left -= chunk.size();
            socket->write(chunk);
        }
        if (*left == 0) socket->disconnectFromHost();
    };
    QObject::connect(m_socket, &QTcpSocket::bytesWritten, m_socket, pump);
    pump();
}
