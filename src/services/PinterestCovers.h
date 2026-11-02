#ifndef PINTEREST_COVERS_H
#define PINTEREST_COVERS_H

#include "../network/NetworkManager.h"
#include <QHash>
#include <QObject>
#include <QRandomGenerator>
#include <QSet>
#include <QStringList>

// Обложки плейлистов с твоей доски на Pinterest. Публичного поиска по слову
// в API v5 нет (он только у партнёров), зато свои доски читаются токеном из
// кабинета разработчика. Каждый раз выдаётся новая картинка, которой ещё не
// было ни у одного плейлиста, а когда доска кончается, начинается новый круг
class PinterestCovers : public QObject {
    Q_OBJECT
public:
    PinterestCovers(NetworkManager* network, const QString& statePath, QObject* parent = nullptr);

    // board: ссылка на доску, её имя или id. Пустой токен выключает обложки
    void setAccount(const QString& token, const QString& board);
    bool ready() const { return !m_images.isEmpty(); }
    // новая картинка для плейлиста key; пусто, пока доска не загружена
    QString next(const QString& key);
    // последняя выданная плейлисту картинка
    QString current(const QString& key) const { return m_assigned.value(key); }
    // эта картинка с доски, а не обложка, которую поставили руками
    bool isOurs(const QString& url) const { return m_images.contains(url) || m_used.contains(url); }

signals:
    void loaded(int count);
    void failed(const QString& message);

private:
    void findBoard();
    void fetchPins(const QString& boardId, const QString& bookmark, QStringList collected);
    static QString imageOf(const QJsonObject& pin);
    QMap<QString, QByteArray> headers() const;
    void load();
    void save() const;

    NetworkManager* net;
    QString m_api;
    QString m_statePath;
    QString m_token;
    QString m_board;
    // поколение запроса: ответы по старому токену или доске отбрасываются
    int m_generation = 0;
    QStringList m_images;
    QSet<QString> m_used;
    QHash<QString, QString> m_assigned;
    QRandomGenerator m_random;
};

#endif
