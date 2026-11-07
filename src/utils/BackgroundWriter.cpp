#include "BackgroundWriter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QSaveFile>
#include <QThreadPool>
#include <variant>

namespace {

using Payload = std::variant<QByteArray, QJsonObject>;

QMutex g_lock;
QHash<QString, Payload> g_pending;

QThreadPool& pool() {
    static QThreadPool* writer = [] {
        auto* p = new QThreadPool;
        p->setMaxThreadCount(1);
        return p;
    }();
    return *writer;
}

void writeLatest(const QString& path) {
    Payload payload;
    {
        QMutexLocker locker(&g_lock);
        auto it = g_pending.find(path);
        // эту версию уже записала предыдущая задача вместе с более свежей
        if (it == g_pending.end()) return;
        payload = std::move(*it);
        g_pending.erase(it);
    }
    QByteArray data = std::holds_alternative<QByteArray>(payload)
                          ? std::get<QByteArray>(payload)
                          : QJsonDocument(std::get<QJsonObject>(payload)).toJson(QJsonDocument::Compact);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(data);
    file.commit();
}

void enqueue(const QString& path, Payload payload) {
    {
        QMutexLocker locker(&g_lock);
        g_pending.insert(path, payload);
    }
    pool().start([path] { writeLatest(path); });
}

} // namespace

void BackgroundWriter::write(const QString& path, const QByteArray& data) {
    enqueue(path, data);
}

void BackgroundWriter::write(const QString& path, const QJsonObject& json) {
    enqueue(path, json);
}

void BackgroundWriter::waitForAll() {
    pool().waitForDone();
}
