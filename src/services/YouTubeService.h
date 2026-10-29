#ifndef YOUTUBE_SERVICE_H
#define YOUTUBE_SERVICE_H

#include "BaseService.h"
#include <QHash>
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

    // как resolveStreamUrl, но в очереди после всего, что включили
    void prefetch(const QString& trackId);
    // найти по "артист - название" и отдать поток под чужим id, чтобы QML
    // доиграл трек так, будто он пришёл из исходного сервиса
    void resolveByQuery(const QString& query, const QString& trackId, bool prefetch = false);
    // поток клипа videoId под чужим id trackId
    void streamAs(const QString& videoId, const QString& trackId, bool prefetch = false);
    // варианты по "артист - название" для трека trackId, ответ в candidatesReady
    void findCandidates(const QString& query, const QString& trackId, bool prefetch = false);
    static QString queryTrackId(const QString& query);
    static QString queryFromTrackId(const QString& trackId);
    // похожее на трек из запроса, ответ в radioReady
    void radioFor(const QString& query, int count);

signals:
    // yt-dlp не нашёл трек, упал или не установлен
    void resolveFailed(const QString& trackId);
    // пустой список, если ничего не нашлось или yt-dlp упал
    void radioReady(const QVariantList& tracks);
    void candidatesReady(const QString& trackId, const QVariantList& tracks);

private:
    struct StreamJob {
        QString trackId;
        QString target; // ссылка на клип, ytsearch1:запрос или поиск вариантов
        bool prefetch = false;
        bool running = false;
        bool search = false; // нужен список вариантов, а не поток
    };

    // ключи в очереди: id трека и цель через перевод строки
    void requestStream(const QString& trackId, const QString& target, bool prefetch, bool search = false);
    void startWaitingPrefetch();
    void startStream(const QString& key);
    void runStream(const QString& key, bool fast);
    void finishStream(const QString& key, const QString& url, int bitrate);
    void runYtDlp(const QStringList& args, const QString& type);
    void runYtDlp(const QStringList& args, std::function<void(bool ok, const QString& output)> done);
    QVariantList parseJsonLines(const QString& output);
    QVariantMap parseYoutubeTrack(const QJsonObject& obj);

    QHash<QString, StreamJob> m_streams;
    QStringList m_prefetchOrder;
    // какой поток трека нужен сейчас: ответ на устаревший запрос не отдаём
    QHash<QString, QString> m_latestTarget;
    int m_fastFailures = 0;
};

#endif
