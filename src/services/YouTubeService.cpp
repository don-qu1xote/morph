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

// Быстрый путь за потоком: клиент visionos отдаёт аудио без JS-плеера, то есть
// без запуска deno и без трёх запросов к разным клиентам, как yt-dlp делает по
// умолчанию. Пустая MORPH_YTDLP_FAST выключает его, другое значение подменяет
static QString fastExtractorArgs() {
    return qEnvironmentVariable("MORPH_YTDLP_FAST", "youtube:player_client=visionos");
}

// столько промахов быстрого пути подряд, и до перезапуска он больше не пробуется
static const int kFastFailureLimit = 3;
// сколько вариантов замены смотреть на YouTube
static const int kCandidateCount = 5;

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
    streamAs(trackId, trackId);
}

void YouTubeService::prefetch(const QString& trackId) {
    QString query = queryFromTrackId(trackId);
    if (!query.isEmpty()) {
        resolveByQuery(query, trackId, true);
        return;
    }
    streamAs(trackId, trackId, true);
}

void YouTubeService::resolveByQuery(const QString& query, const QString& trackId, bool prefetch) {
    requestStream(trackId, QString("ytsearch1:%1").arg(query), prefetch);
}

void YouTubeService::streamAs(const QString& videoId, const QString& trackId, bool prefetch) {
    requestStream(trackId, QString("https://www.youtube.com/watch?v=%1").arg(videoId), prefetch);
}

// плоский поиск без потоков: один запрос и без deno
void YouTubeService::findCandidates(const QString& query, const QString& trackId, bool prefetch) {
    requestStream(trackId, QString("ytsearch%1:%2").arg(kCandidateCount).arg(query), prefetch, true);
}

// Один трек ищет один yt-dlp: раньше предзагрузка и клик по тому же треку
// запускали два процесса. Включённый трек идёт без очереди, а предзагрузка
// ждёт, пока ничего не ищется, чтобы не отнимать у него процессор и сеть
void YouTubeService::requestStream(const QString& trackId, const QString& target, bool prefetch, bool search) {
    QString key = trackId + '\n' + target;
    if (!search) m_latestTarget[trackId] = target;

    auto it = m_streams.find(key);
    if (it != m_streams.end() && (prefetch || !it->prefetch)) return;

    // играет один трек: прошлые клики ещё ищутся, но уже на правах предзагрузки
    if (!prefetch) {
        for (auto job = m_streams.begin(); job != m_streams.end(); ++job) job->prefetch = true;
    }

    if (it != m_streams.end()) {
        it->prefetch = false;
        if (!it->running) {
            m_prefetchOrder.removeAll(key);
            startStream(key);
        }
        return;
    }

    m_streams.insert(key, {trackId, target, prefetch, false, search});
    if (!prefetch) {
        startStream(key);
        return;
    }
    m_prefetchOrder.append(key);
    startWaitingPrefetch();
}

void YouTubeService::startWaitingPrefetch() {
    for (const StreamJob& job : std::as_const(m_streams)) {
        if (job.running) return;
    }
    if (!m_prefetchOrder.isEmpty()) startStream(m_prefetchOrder.takeFirst());
}

void YouTubeService::startStream(const QString& key) {
    StreamJob& job = m_streams[key];
    job.running = true;
    if (!job.search) {
        runStream(key, !fastExtractorArgs().isEmpty() && m_fastFailures < kFastFailureLimit);
        return;
    }

    runYtDlp(listArgs() << job.target, [this, key](bool ok, const QString& output) {
        StreamJob done = m_streams.take(key);
        emit candidatesReady(done.trackId, ok ? parseJsonLines(output) : QVariantList());
        startWaitingPrefetch();
    });
}

void YouTubeService::runStream(const QString& key, bool fast) {
    QStringList args;
    args << "-f" << kAudioFormat << "--quiet" << "--no-warnings" << "--no-playlist" << "--user-agent" << kUserAgent;
    if (fast) args << "--extractor-args" << fastExtractorArgs();
    args << "--print-json" << m_streams.value(key).target;

    runYtDlp(args, [this, key, fast](bool ok, const QString& output) {
        QJsonObject obj = QJsonDocument::fromJson(output.toUtf8()).object();
        QString url = ok ? obj["url"].toString() : QString();
        // быстрый клиент отдаёт не всё (детский контент, проверка на бота):
        // тот же запрос ещё раз, но с клиентами по умолчанию
        if (url.isEmpty() && fast) {
            ++m_fastFailures;
            runStream(key, false);
            return;
        }
        if (fast) m_fastFailures = 0;
        finishStream(key, url, qRound(obj["abr"].toDouble()));
    });
}

void YouTubeService::finishStream(const QString& key, const QString& url, int bitrate) {
    StreamJob job = m_streams.take(key);
    // пока искался этот поток, для трека попросили другой: выбрали другую версию
    if (m_latestTarget.value(job.trackId) == job.target) {
        m_latestTarget.remove(job.trackId);
        if (url.isEmpty()) {
            emit resolveFailed(job.trackId);
        } else {
            emit streamUrlReady(job.trackId, url);
            if (bitrate > 0) emit bitrateReady(job.trackId, bitrate);
        }
    }
    startWaitingPrefetch();
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

void YouTubeService::runYtDlp(const QStringList& args, const QString& type) {
    runYtDlp(args, [this, type](bool ok, const QString& output) {
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
