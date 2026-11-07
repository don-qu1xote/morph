#ifndef SERVER_AUDIO_H
#define SERVER_AUDIO_H

#include "SubsonicBackend.h"
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class CacheManager;
class QTimer;
class ServiceManager;

// Файлы для телефона. Трек берётся из кэша morph, а если его там нет, ищется
// так же, как для окна, и сначала целиком ложится на мак: телефон получает
// готовый файл и перематывает его по Range. То, что iPhone не играет (webm,
// ogg, ts) или играет плохо (MP4 кусками, как отдают YouTube и SoundCloud),
// ffmpeg перекладывает в обычный m4a
class ServerAudio : public QObject {
    Q_OBJECT
public:
    ServerAudio(ServiceManager* services, CacheManager* cache, const QString& dir, QObject* parent = nullptr);

    void fetch(const QVariantMap& track, SubsonicBackend::AudioDone done);
    SubsonicBackend::AudioInfo info(const QVariantMap& track) const;
    // скачать заранее по одному: миксы на телефон должны отдаваться сразу,
    // а не ждать yt-dlp, пока Amperfy не устанет
    void prefetch(const QVariantList& tracks);

    struct Format {
        QByteArray contentType;
        QString suffix;
        // iPhone откроет файл как есть
        bool playable = false;
        // MP4 из кусков (moof): играет, но длительность и перемотка врут
        bool fragmented = false;
    };
    // формат по первым байтам: имя файла в кэше всегда .mp3, что бы там ни лежало
    static Format sniff(const QString& path);
    // пусто, если ffmpeg не найден; тесты подставляют свой путь
    void setFfmpeg(const QString& path) { m_ffmpeg = path; }

private:
    struct Job {
        QVariantMap track;
        QList<SubsonicBackend::AudioDone> waiting;
        QTimer* timer = nullptr;
    };
    QString cachedPath(const QString& trackId) const;
    void streamReady(const QString& trackId, const QString& url);
    void finish(const QString& trackId, const QString& path);
    void fail(const QString& trackId);
    void prepare(const QString& source, SubsonicBackend::AudioDone done);
    void prefetchNext();
    Format format(const QString& path) const;
    // перегнанные копии копятся отдельно от кэша: старые уходят, когда их много
    void trim();

    ServiceManager* m_services;
    CacheManager* m_cache;
    QString m_dir;
    QString m_ffmpeg;
    QHash<QString, Job> m_jobs;
    // не нашлось: телефон узнаёт сразу, а не через полторы минуты ожидания
    QHash<QString, qint64> m_failed;
    // один ffmpeg на файл, сколько бы запросов его ни ждало
    QHash<QString, QList<SubsonicBackend::AudioDone>> m_converting;
    // формат файла по пути и времени изменения: каталог спрашивает его на каждый трек
    mutable QHash<QString, QPair<qint64, Format>> m_formats;
    QVariantList m_prefetch;
    bool m_prefetchBusy = false;
};

#endif
