#ifndef LYRICS_SERVICE_H
#define LYRICS_SERVICE_H

#include "../network/NetworkManager.h"
#include <QHash>
#include <QObject>
#include <QVariantList>

// Тексты песен. У трека из Яндекса сначала спрашиваем Яндекс (синхронный LRC),
// у остальных и если Яндекс промолчал, открытую базу LRCLIB: в ней много
// синхронных текстов, в том числе русских, и она не просит ключей
class LyricsService : public QObject {
    Q_OBJECT
public:
    explicit LyricsService(NetworkManager* network, QObject* parent = nullptr);

    void setYandexToken(const QString& token) { m_yandexToken = token; }
    // track: id, service, artist, title, durationMs
    void fetch(const QVariantMap& track);

    // строки [{timeMs, text}] по времени; у текста без меток timeMs = -1
    static QVariantList parseLrc(const QString& text);

signals:
    // source: "Yandex" или "LRCLIB"; synced, если у строк есть время
    void lyricsReady(const QString& trackId, const QVariantList& lines, bool synced, const QString& source);
    void lyricsMissing(const QString& trackId);

private:
    void fetchFromYandex(const QVariantMap& track);
    void fetchFromLrclib(const QVariantMap& track, bool searchInstead = false);
    void deliver(const QString& trackId, const QString& text, const QString& source);

    NetworkManager* net;
    QString m_yandexApi;
    QString m_lrclibApi;
    QString m_yandexToken;
    // один раз нашли, дальше отдаём из памяти: панель открывают и закрывают часто
    QHash<QString, QVariantMap> m_found;
};

#endif
