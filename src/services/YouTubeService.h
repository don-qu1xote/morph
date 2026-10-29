#ifndef YOUTUBE_SERVICE_H
#define YOUTUBE_SERVICE_H

#include "BaseService.h"
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>

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
    // похожее на трек из запроса, ответ в radioReady
    void radioFor(const QString& query, int count);

signals:
    // yt-dlp не нашёл трек, упал или не установлен
    void resolveFailed(const QString& trackId);
    // пустой список, если ничего не нашлось или yt-dlp упал
    void radioReady(const QVariantList& tracks);

private:
    void runYtDlp(const QStringList& args, const QString& type, const QString& id = QString());
    void runYtDlp(const QStringList& args, std::function<void(bool ok, const QString& output)> done);
    QVariantList parseJsonLines(const QString& output);
    QVariantMap parseYoutubeTrack(const QJsonObject& obj);
};

#endif
