#ifndef YOUTUBE_SERVICE_H
#define YOUTUBE_SERVICE_H

#include "BaseService.h"
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

class YouTubeService : public BaseService {
    Q_OBJECT
public:
    explicit YouTubeService(QObject* parent = nullptr);
    void search(const QString& query) override;
    void resolveStreamUrl(const QString& trackId) override;
    void getCharts() override;
    void getWave() override;
    void getDailyMixes() override;
    void reportPlay(const QString& trackId, const QString& albumId) override;
    void importPlaylist(const QString& url) override;
    bool isTrackCached(const QString& trackId) override;

    // найти по "артист - название" и отдать поток под чужим id, чтобы QML
    // доиграл трек так, будто он пришёл из исходного сервиса
    void resolveByQuery(const QString& query, const QString& trackId);
    static QString queryTrackId(const QString& query);
    static QString queryFromTrackId(const QString& trackId);

private:
    void runYtDlp(const QStringList& args, const QString& type, const QString& id = QString());
    QVariantMap parseYoutubeTrack(const QJsonObject& obj);
};

#endif
