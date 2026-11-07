#ifndef SUBSONIC_BACKEND_H
#define SUBSONIC_BACKEND_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Что серверу для телефона нужно от morph. Сервер не знает ни про Яндекс, ни
// про кэш: в приложении это MorphBackend, в тестах своя подделка
class SubsonicBackend {
public:
    virtual ~SubsonicBackend() = default;

    // растёт, когда меняются MAIN, плейлисты или миксы: каталог тогда собирается заново
    virtual quint64 revision() const = 0;

    // MAIN, новые сверху
    virtual QVariantList library() const = 0;
    virtual QStringList playlistNames() const = 0;
    virtual QVariantList playlistTracks(const QString& name) const = 0;
    virtual QString playlistCover(const QString& name) const = 0;
    // миксы для телефона: id, name, created (мс), coverUrl, tracks
    virtual QVariantList mixes() const = 0;

    virtual bool isLiked(const QVariantMap& track) const = 0;
    virtual void setLiked(const QVariantMap& track, bool liked) = 0;
    // прослушивание на телефоне: сколько секунд из скольких и дослушан ли трек
    virtual void listened(const QVariantMap& track, double played, double length, bool completed) = 0;

    // имя, под которым плейлист сохранён: занятое получает номер, как в окне morph
    virtual QString createPlaylist(const QString& name, const QVariantList& tracks) = 0;
    // новое имя и весь состав разом: Subsonic присылает правки пачкой
    virtual bool updatePlaylist(const QString& name, const QString& newName, const QVariantList& tracks) = 0;
    virtual void deletePlaylist(const QString& name) = 0;

    struct AudioInfo {
        QByteArray contentType;
        QString suffix;
        qint64 size = 0;
    };
    // что известно о файле до запроса: тип и размер, если трек уже скачан на мак
    virtual AudioInfo audioInfo(const QVariantMap& track) const = 0;
    // файл трека для телефона; пустой путь, если достать не вышло
    using AudioDone = std::function<void(const QString& path, const QByteArray& contentType)>;
    virtual void audio(const QVariantMap& track, AudioDone done) = 0;
    // обложка по адресу из трека, скачанная на мак; пустой путь, если нет
    using CoverDone = std::function<void(const QString& path)>;
    virtual void cover(const QString& url, CoverDone done) = 0;
};

#endif
