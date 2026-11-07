#include "ServerAudio.h"
#include "../core/ServiceManager.h"
#include "../utils/CacheManager.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>

// столько телефон ждёт трек, которого нет в кэше: поиск в yt-dlp плюс скачивание
static const int kFetchTimeoutMs = 90000;
// не нашлось: столько отвечать сразу, а не искать заново
static const qint64 kFailedForMs = 3600 * 1000;
static const int kConvertTimeoutMs = 120000;
static const qint64 kConvertedLimit = 1024LL * 1024 * 1024;

ServerAudio::ServerAudio(ServiceManager* services, CacheManager* cache, const QString& dir, QObject* parent)
    : QObject(parent), m_services(services), m_cache(cache), m_dir(dir),
      m_ffmpeg(QStandardPaths::findExecutable("ffmpeg")) {
    QDir().mkpath(m_dir);
    connect(services, &ServiceManager::phoneStreamReady, this, &ServerAudio::streamReady);
    connect(services, &ServiceManager::phoneStreamFailed, this, &ServerAudio::fail);
    connect(cache, &CacheManager::trackCached, this, [this](const QString& trackId, const QString& url) {
        if (m_jobs.contains(trackId)) finish(trackId, QUrl(url).toLocalFile());
    });
    trim();
}

static bool hasMoof(QFile& file) {
    qint64 pos = 0;
    for (int i = 0; i < 64 && file.seek(pos); ++i) {
        QByteArray head = file.read(16);
        if (head.size() < 8) return false;
        auto u32 = [&](int at) {
            return (quint64(quint8(head[at])) << 24) | (quint64(quint8(head[at + 1])) << 16) |
                   (quint64(quint8(head[at + 2])) << 8) | quint64(quint8(head[at + 3]));
        };
        quint64 size = u32(0);
        QByteArray type = head.mid(4, 4);
        if (type == "moof") return true;
        if (size == 1 && head.size() >= 16) size = (u32(8) << 32) | u32(12);
        if (size < 8) return false;
        pos += qint64(size);
    }
    return false;
}

ServerAudio::Format ServerAudio::sniff(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray h = file.read(512);
    if (h.size() < 12) return {};
    auto u = [&](int i) { return quint8(h[i]); };
    if (h.startsWith("ID3")) return {"audio/mpeg", "mp3", true, false};
    if (u(0) == 0xFF && (u(1) & 0xE0) == 0xE0) {
        // у ADTS (AAC без контейнера) поле слоя нулевое, у MP3 нет
        if (((u(1) >> 1) & 3) == 0) return {"audio/aac", "aac", true, false};
        return {"audio/mpeg", "mp3", true, false};
    }
    if (h.mid(4, 4) == "ftyp") return {"audio/mp4", "m4a", true, hasMoof(file)};
    if (h.startsWith("fLaC")) return {"audio/flac", "flac", true, false};
    if (h.startsWith("RIFF") && h.mid(8, 4) == "WAVE") return {"audio/wav", "wav", true, false};
    if (h.startsWith("OggS")) return {"audio/ogg", "ogg", false, false};
    if (h.startsWith("\x1A\x45\xDF\xA3")) return {"audio/webm", "webm", false, false};
    if (u(0) == 0x47 && h.size() > 188 && u(188) == 0x47) return {"video/mp2t", "ts", false, false};
    return {"application/octet-stream", "bin", false, false};
}

ServerAudio::Format ServerAudio::format(const QString& path) const {
    qint64 modified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    auto it = m_formats.constFind(path);
    if (it != m_formats.cend() && it->first == modified) return it->second;
    Format f = sniff(path);
    m_formats.insert(path, {modified, f});
    return f;
}

QString ServerAudio::cachedPath(const QString& trackId) const {
    QString path = m_cache->getTrackPath(trackId);
    return QFileInfo(path).size() > 4096 ? path : QString();
}

static QString convertedPath(const QString& dir, const QString& source) {
    return dir + "/" + QFileInfo(source).completeBaseName() + ".m4a";
}

static bool convertedIsFresh(const QString& target, const QString& source) {
    QFileInfo t(target);
    return t.size() > 4096 && t.lastModified() >= QFileInfo(source).lastModified();
}

SubsonicBackend::AudioInfo ServerAudio::info(const QVariantMap& track) const {
    QString path = cachedPath(track.value("id").toString());
    if (path.isEmpty()) {
        // ещё не скачан: Яндекс отдаёт mp3, остальные почти всегда AAC в MP4
        if (track.value("service").toString() == "Yandex") return {"audio/mpeg", "mp3", 0};
        return {"audio/mp4", "m4a", 0};
    }
    QString converted = convertedPath(m_dir, path);
    if (convertedIsFresh(converted, path)) return {"audio/mp4", "m4a", QFileInfo(converted).size()};
    Format f = format(path);
    if (f.playable && !f.fragmented) return {f.contentType, f.suffix, QFileInfo(path).size()};
    if (!m_ffmpeg.isEmpty()) return {"audio/mp4", "m4a", 0};
    return {f.contentType, f.suffix, QFileInfo(path).size()};
}

void ServerAudio::fetch(const QVariantMap& track, SubsonicBackend::AudioDone done) {
    QString trackId = track.value("id").toString();
    if (trackId.isEmpty()) {
        done({}, {});
        return;
    }
    QString path = cachedPath(trackId);
    if (!path.isEmpty()) {
        prepare(path, std::move(done));
        return;
    }
    auto failed = m_failed.constFind(trackId);
    if (failed != m_failed.cend() && QDateTime::currentMSecsSinceEpoch() - failed.value() < kFailedForMs) {
        done({}, {});
        return;
    }

    Job& job = m_jobs[trackId];
    job.waiting.append(std::move(done));
    if (job.timer) return;
    job.track = track;
    job.timer = new QTimer(this);
    job.timer->setSingleShot(true);
    connect(job.timer, &QTimer::timeout, this, [this, trackId] { fail(trackId); });
    job.timer->start(kFetchTimeoutMs);
    // ответ может прийти прямо отсюда, если трек уже есть: дальше job не трогаем
    m_services->fetchForPhone(track.value("service").toString(), trackId);
}

void ServerAudio::streamReady(const QString& trackId, const QString& url) {
    if (!m_jobs.contains(trackId)) return;
    if (url.startsWith("file://")) {
        finish(trackId, QUrl(url).toLocalFile());
        return;
    }
    // Ссылка живёт недолго, а телефон качает медленно и с перемоткой, поэтому
    // сначала файл целиком в кэш. Окно, если тоже просило, качает тот же файл
    m_cache->cacheTrack(trackId, url);
    QString path = cachedPath(trackId);
    if (!path.isEmpty() && m_cache->isTrackCached(trackId)) finish(trackId, path);
}

void ServerAudio::finish(const QString& trackId, const QString& path) {
    Job job = m_jobs.take(trackId);
    if (job.timer) job.timer->deleteLater();
    m_failed.remove(trackId);
    for (auto& done : job.waiting) prepare(path, std::move(done));
}

void ServerAudio::fail(const QString& trackId) {
    if (!m_jobs.contains(trackId)) return;
    Job job = m_jobs.take(trackId);
    if (job.timer) job.timer->deleteLater();
    m_failed.insert(trackId, QDateTime::currentMSecsSinceEpoch());
    for (auto& done : job.waiting) done({}, {});
}

void ServerAudio::prepare(const QString& source, SubsonicBackend::AudioDone done) {
    Format f = format(source);
    if ((f.playable && !f.fragmented) || m_ffmpeg.isEmpty()) {
        done(source, f.contentType.isEmpty() ? QByteArray("application/octet-stream") : f.contentType);
        return;
    }
    QString target = convertedPath(m_dir, source);
    if (convertedIsFresh(target, source)) {
        done(target, "audio/mp4");
        return;
    }
    auto& waiting = m_converting[target];
    waiting.append(std::move(done));
    if (waiting.size() > 1) return;

    // AAC из кусочного MP4 просто перекладывается, остальное кодируется заново
    QStringList args{"-nostdin", "-v", "error", "-y", "-i", source, "-vn", "-map", "0:a:0"};
    if (f.fragmented) args << "-c:a" << "copy";
    else args << "-c:a" << "aac" << "-b:a" << "192k";
    QString part = target + ".part";
    args << "-movflags" << "+faststart" << "-f" << "mp4" << part;

    auto* process = new QProcess(this);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    auto finished = [this, process, source, target, part, f](bool ok) {
        ok = ok && QFileInfo(part).size() > 4096;
        if (ok) {
            QFile::remove(target);
            ok = QFile::rename(part, target);
        }
        QFile::remove(part);
        // не вышло: телефон получит исходник, вдруг сыграет
        const auto waiters = m_converting.take(target);
        for (const auto& done : waiters) {
            if (ok) done(target, "audio/mp4");
            else done(source, f.contentType);
        }
        process->deleteLater();
        if (ok) trim();
    };
    connect(process, &QProcess::errorOccurred, this, [finished](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finished(false);
    });
    connect(process, &QProcess::finished, this, [finished](int code, QProcess::ExitStatus status) {
        finished(status == QProcess::NormalExit && code == 0);
    });
    process->start(m_ffmpeg, args);
    timer->start(kConvertTimeoutMs);
}

void ServerAudio::trim() {
    QFileInfoList files = QDir(m_dir).entryInfoList({"*.m4a"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo& f : files) total += f.size();
    for (const QFileInfo& f : files) {
        if (total <= kConvertedLimit) break;
        total -= f.size();
        QFile::remove(f.filePath());
    }
}

void ServerAudio::prefetch(const QVariantList& tracks) {
    QStringList queued;
    for (const QVariant& t : std::as_const(m_prefetch)) queued.append(t.toMap().value("id").toString());
    for (const QVariant& t : tracks) {
        QString trackId = t.toMap().value("id").toString();
        if (!trackId.isEmpty() && !queued.contains(trackId)) {
            m_prefetch.append(t);
            queued.append(trackId);
        }
    }
    prefetchNext();
}

void ServerAudio::prefetchNext() {
    if (m_prefetchBusy || m_prefetch.isEmpty()) return;
    m_prefetchBusy = true;
    QVariantMap track = m_prefetch.takeFirst().toMap();
    fetch(track, [this](const QString&, const QByteArray&) {
        m_prefetchBusy = false;
        // через цикл событий: готовые из кэша иначе ушли бы в глубокую рекурсию
        QTimer::singleShot(0, this, &ServerAudio::prefetchNext);
    });
}
