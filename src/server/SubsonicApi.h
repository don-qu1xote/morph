#ifndef SUBSONIC_API_H
#define SUBSONIC_API_H

#include "HttpServer.h"
#include "SubsonicBackend.h"
#include "SubsonicCatalog.h"
#include <QHash>
#include <QObject>
#include <functional>

// Subsonic API поверх каталога: то подмножество, которым пользуются Amperfy и
// другие клиенты на телефоне. Ответы в XML, как ждёт Amperfy, или в JSON по f=json
class SubsonicApi : public QObject {
    Q_OBJECT
public:
    // dataDir: там лежат даты, когда сервер впервые увидел песни
    SubsonicApi(SubsonicBackend* backend, const QString& dataDir, QObject* parent = nullptr);
    void setCredentials(const QString& user, const QString& password);
    void handle(const IncomingRequest& request, std::shared_ptr<HttpResponder> responder);
    // время в мс; тесты подставляют свои часы
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    const SubsonicCatalog& catalog();

    static const char* kApiVersion;

signals:
    // телефон зашёл: c из запроса, например Amperfy
    void clientSeen(const QString& client);

private:
    struct Call;
    struct Node;
    using Handler = void (SubsonicApi::*)(Call&);

    void refresh();
    bool authorized(Call& call);
    void reply(Call& call, Node payload);
    void ok(Call& call);
    void error(Call& call, int code, const QString& message);

    Node songNode(const SubsonicCatalog::Song& song, const QString& tag) const;
    Node albumNode(const SubsonicCatalog::Album& album) const;
    Node artistNode(const SubsonicCatalog::Artist& artist) const;
    Node playlistNode(const SubsonicCatalog::Playlist& playlist) const;
    Node playlistWithSongs(const SubsonicCatalog::Playlist& playlist) const;
    QList<QVariantMap> tracksFor(const QStringList& songIds) const;
    // трек по id песни, в том числе той, что уже пропала из каталога
    QVariantMap trackOf(const QString& songId) const;
    void finishPlaying(qint64 now);
    void saveSeen() const;

    void ping(Call& call);
    void license(Call& call);
    void musicFolders(Call& call);
    void genres(Call& call);
    void artists(Call& call);
    void indexes(Call& call);
    void artist(Call& call);
    void album(Call& call);
    void song(Call& call);
    void musicDirectory(Call& call);
    void albumList(Call& call);
    void playlists(Call& call);
    void playlist(Call& call);
    void createPlaylist(Call& call);
    void updatePlaylist(Call& call);
    void deletePlaylist(Call& call);
    void starred(Call& call);
    void star(Call& call);
    void unstar(Call& call);
    void search(Call& call);
    void randomSongs(Call& call);
    void scrobble(Call& call);
    void stream(Call& call);
    void coverArt(Call& call);
    void user(Call& call);
    void scanStatus(Call& call);
    void empty(Call& call);

    SubsonicBackend* m_backend;
    QString m_seenPath;
    QString m_user;
    QString m_password;
    std::function<qint64()> m_clock;
    SubsonicCatalog m_catalog;
    quint64 m_builtRevision = 0;
    bool m_built = false;
    QHash<QString, qint64> m_seen;
    // Песни, которых больше нет в каталоге. Снятая на телефоне звезда убирает
    // песню из MAIN, а с ней и из каталога, но телефон ещё знает её по id и
    // может поставить звезду обратно или добавить её в плейлист
    QHash<QString, QVariantMap> m_gone;
    QHash<QString, Handler> m_handlers;
    // трек, который телефон начал играть и ещё не дослушал: по нему видно пропуски
    QString m_playingId;
    qint64 m_playingSince = 0;
};

#endif
