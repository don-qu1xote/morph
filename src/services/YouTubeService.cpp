#include "YouTubeService.h"
#include "../utils/PathProvider.h"
#include <QDebug>
#include <QUrl>
#include <QFile>
#include <QTimer>

YouTubeService::YouTubeService(QObject* parent) : BaseService(parent) {}

static const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36";
// m4a первым: AAC играет на любом бэкенде QtMultimedia, а webm/opus только с FFmpeg
static const char* kAudioFormat = "bestaudio[ext=m4a]/bestaudio";
static const QString kQueryPrefix = "ytq-";

// поиск, чарты, импорт и миксы: список треков без потоков, по JSON на строку
static QStringList listArgs() {
    return {"--print-json", "--flat-playlist", "--quiet", "--no-warnings", "--user-agent", kUserAgent};
}

void YouTubeService::search(const QString& query) {
    runYtDlp(listArgs() << QString("ytsearch30:%1").arg(query), "search");
}

// id из самого запроса: его можно хранить в плейлисте и сразу знать, что искать.
// base64url, потому что по id ещё и файл кэша называется
QString YouTubeService::queryTrackId(const QString& query) {
    auto options = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
    return kQueryPrefix + QString::fromLatin1(query.toUtf8().toBase64(options));
}

QString YouTubeService::queryFromTrackId(const QString& trackId) {
    if (!trackId.startsWith(kQueryPrefix)) return {};
    QByteArray encoded = trackId.mid(kQueryPrefix.size()).toLatin1();
    return QString::fromUtf8(QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding));
}

void YouTubeService::resolveStreamUrl(const QString& trackId) {
    QString query = queryFromTrackId(trackId);
    if (!query.isEmpty()) {
        resolveByQuery(query, trackId);
        return;
    }

    QStringList args;
    args << "-f" << kAudioFormat << "--quiet" << "--no-warnings" << "--user-agent" << kUserAgent << "--print-json"
         << QString("https://www.youtube.com/watch?v=%1").arg(trackId);
    runYtDlp(args, "resolve", trackId);
}

void YouTubeService::resolveByQuery(const QString& query, const QString& trackId) {
    QStringList args;
    args << "-f" << kAudioFormat << "--quiet" << "--no-warnings" << "--no-playlist" << "--user-agent" << kUserAgent
         << "--print-json" << QString("ytsearch1:%1").arg(query);
    runYtDlp(args, "resolve", trackId);
}

void YouTubeService::getCharts() {
    runYtDlp(listArgs() << "https://music.youtube.com/playlist?list=PL4fGSI1pDJn6jWjX9jU1A9nF56E-7K8Xm", "charts");
}

void YouTubeService::getWave() {
    emit waveReady("YouTube", {});
}

void YouTubeService::getDailyMixes() {
    emit dailyMixesReady("YouTube", {});
}

void YouTubeService::reportPlay(const QString& trackId, const QString& albumId) {
    Q_UNUSED(trackId);
    Q_UNUSED(albumId);
}

void YouTubeService::importPlaylist(const QString& url) {
    runYtDlp(listArgs() << url, "import");
}

// Похожее для смешанной волны: находим клип по запросу и берём его микс, плейлист
// RD<id>, который YouTube собирает под каждый трек. Первым в миксе идёт сам клип
void YouTubeService::radioFor(const QString& query, int count) {
    runYtDlp(listArgs() << QString("ytsearch1:%1").arg(query), [this, count](bool ok, const QString& output) {
        QString seedId = ok ? parseJsonLines(output).value(0).toMap()["id"].toString() : QString();
        if (seedId.isEmpty()) {
            emit radioReady({});
            return;
        }

        QStringList args = listArgs();
        args << "--playlist-items" << QString("2-%1").arg(count + 1)
             << QString("https://www.youtube.com/watch?v=%1&list=RD%1").arg(seedId);
        runYtDlp(args, [this](bool ok, const QString& output) {
            emit radioReady(ok ? parseJsonLines(output) : QVariantList());
        });
    });
}

void YouTubeService::runYtDlp(const QStringList& args, const QString& type, const QString& id) {
    runYtDlp(args, [this, type, id](bool ok, const QString& output) {
        if (type == "resolve") {
            QJsonObject obj = QJsonDocument::fromJson(output.toUtf8()).object();
            QString url = obj["url"].toString();
            if (!ok || url.isEmpty()) {
                emit resolveFailed(id);
                return;
            }
            emit streamUrlReady(id, url);
            int bitrate = qRound(obj["abr"].toDouble());
            if (bitrate > 0) emit bitrateReady(id, bitrate);
            return;
        }

        if (!ok) return;
        QVariantList results = parseJsonLines(output);
        if (type == "search") emit searchResultsReady("YouTube", results);
        else if (type == "charts") emit chartsReady("YouTube", results);
        else if (type == "import") emit playlistImported("Imported Playlist", "", results);
    });
}

// Процесс, таймаут и уборка отдельно от того, что делать с выводом.
// ok ложный, если yt-dlp не запустился, упал или не уложился в 20 секунд
void YouTubeService::runYtDlp(const QStringList& args, std::function<void(bool, const QString&)> done) {
    QProcess* process = new QProcess(this);
    process->setWorkingDirectory(PathProvider::getTrackCachePath());
    
    QTimer* timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });

    // без yt-dlp в системе finished не приходит вовсе, и процесс так и висел бы в памяти
    connect(process, &QProcess::errorOccurred, this, [process, timer, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        timer->stop();
        done(false, {});
        process->deleteLater();
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [process, timer, done]() {
        timer->stop();
        bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
        done(ok, ok ? QString::fromUtf8(process->readAllStandardOutput()) : QString());
        process->deleteLater();
    });

    process->start("yt-dlp", args);
    timer->start(20000);
}

QVariantList YouTubeService::parseJsonLines(const QString& output) {
    QVariantList results;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isNull()) results.append(parseYoutubeTrack(doc.object()));
    }
    return results;
}

bool YouTubeService::isTrackCached(const QString& trackId) {
    QString path = PathProvider::getTrackCachePath() + "/" + trackId + ".mp3";
    return QFile::exists(path);
}

QVariantMap YouTubeService::parseYoutubeTrack(const QJsonObject& obj) {
    QVariantMap track;
    track["id"] = obj["id"].toString();
    track["title"] = obj["title"].toString();
    
    QString artist = obj["uploader"].toString();
    if (artist.isEmpty()) artist = obj["channel"].toString();
    // автоканалы YouTube Music называются «Артист - Topic», в миксах их большинство
    if (artist.endsWith(" - Topic")) artist.chop(8);
    track["artist"] = artist;
    
    track["album"] = "YouTube Music";
    
    QJsonArray thumbnails = obj["thumbnails"].toArray();
    if (!thumbnails.isEmpty()) {
        track["coverUrl"] = thumbnails.last().toObject()["url"].toString();
    } else {
        track["coverUrl"] = obj["thumbnail"].toString();
    }
    
    track["service"] = "YouTube";
    
    QString webUrl = obj["webpage_url"].toString();
    if (webUrl.isEmpty()) webUrl = QString("https://www.youtube.com/watch?v=%1").arg(track["id"].toString());
    track["webUrl"] = webUrl;
    
    track["durationMs"] = static_cast<qint64>(obj["duration"].toDouble() * 1000);
    
    return track;
}
