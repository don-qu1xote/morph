#include "SoundLibrary.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>
#include <cstring>

// пауза между файлами: фоновая работа не должна быть заметна по звуку и вентилятору
static const int kGapMs = 300;
// ffmpeg, который висит дольше, убивается: файл битый или диск занят
static const int kDecodeTimeoutMs = 30000;
// сколько секунд раскладывать и с какой брать: минута после вступления
static const int kDecodeSeconds = 90;
static const int kSkipSeconds = 30;
static const int kAnalyzeSeconds = 60;
// файл меньше этого ещё качается или пустой
static const qint64 kMinFileBytes = 4096;

SoundLibrary::SoundLibrary(const QString& dbPath, QObject* parent)
    : QObject(parent), m_connection(QString("sound-%1").arg(quintptr(this))) {
    connect(&m_watcher, &QFutureWatcher<AudioFeatures::Sound>::finished, this,
            [this]() { finish(m_watcher.result()); });

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        // без SQLite звук живёт до выхода, остальное работает
        qWarning() << "sound library:" << m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("CREATE TABLE IF NOT EXISTS sounds (key TEXT PRIMARY KEY, modified INTEGER NOT NULL, "
           "valid INTEGER NOT NULL, bpm REAL, energy REAL, loudness REAL, onsets REAL, timbre TEXT)");
    q.exec("SELECT key, modified, valid, bpm, energy, loudness, onsets, timbre FROM sounds");
    while (q.next()) {
        Entry e;
        e.modified = q.value(1).toLongLong();
        e.sound.valid = q.value(2).toBool();
        e.sound.bpm = q.value(3).toDouble();
        e.sound.energy = q.value(4).toDouble();
        e.sound.loudness = q.value(5).toDouble();
        e.sound.onsetRate = q.value(6).toDouble();
        const QJsonArray timbre = QJsonDocument::fromJson(q.value(7).toByteArray()).array();
        for (int d = 0; d < AudioFeatures::kTimbreDim && d < timbre.size(); ++d)
            e.sound.timbre[d] = float(timbre[d].toDouble());
        if (e.sound.valid) ++m_valid;
        m_entries.insert(q.value(0).toString(), e);
    }
}

SoundLibrary::~SoundLibrary() {
    m_queue.clear();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_watcher.disconnect(this);
    m_watcher.waitForFinished();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

void SoundLibrary::enqueue(const QString& key, const QString& path) {
    if (m_ffmpeg.isEmpty() || key.isEmpty()) return;
    QFileInfo file(path);
    if (file.size() < kMinFileBytes) return;
    qint64 modified = file.lastModified().toSecsSinceEpoch();
    auto it = m_entries.constFind(key);
    if (it != m_entries.cend() && it->modified == modified) return;
    if ((m_busy && m_key == key) ||
        std::any_of(m_queue.cbegin(), m_queue.cend(), [&key](const auto& job) { return job.first == key; }))
        return;
    m_queue.append({key, path});
    if (!m_busy) QTimer::singleShot(kGapMs, this, &SoundLibrary::next);
}

void SoundLibrary::scan(const QString& dir) {
    // все файлы кэша лежат с .mp3, что бы внутри ни было: ffmpeg разберётся сам
    const QFileInfoList files = QDir(dir).entryInfoList({"*.mp3"}, QDir::Files, QDir::Time);
    for (const QFileInfo& f : files) enqueue(f.completeBaseName(), f.filePath());
}

void SoundLibrary::next() {
    if (m_busy || m_queue.isEmpty()) return;
    const auto [key, path] = m_queue.takeFirst();
    QFileInfo file(path);
    if (file.size() < kMinFileBytes) {
        next();
        return;
    }
    m_busy = true;
    m_key = key;
    m_modified = file.lastModified().toSecsSinceEpoch();

    m_process = new QProcess(this);
    auto pcm = std::make_shared<QByteArray>();
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            [this, pcm]() { pcm->append(m_process->readAllStandardOutput()); });
    connect(m_process, &QProcess::finished, this, [this, pcm](int code, QProcess::ExitStatus status) {
        pcm->append(m_process->readAllStandardOutput());
        m_process->deleteLater();
        m_process = nullptr;
        decoded(code == 0 && status == QProcess::NormalExit ? *pcm : QByteArray());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // не запустился вовсе: finished не придёт
        if (error != QProcess::FailedToStart || !m_process) return;
        m_process->deleteLater();
        m_process = nullptr;
        decoded({});
    });
    QTimer::singleShot(kDecodeTimeoutMs, m_process, &QProcess::kill);
    m_process->start(m_ffmpeg, {"-nostdin", "-v", "error", "-t", QString::number(kDecodeSeconds), "-i", path, "-ac",
                                "1", "-ar", QString::number(AudioFeatures::kSampleRate), "-f", "f32le", "pipe:1"});
}

void SoundLibrary::decoded(const QByteArray& pcm) {
    std::vector<float> samples(size_t(pcm.size()) / sizeof(float));
    if (!samples.empty()) std::memcpy(samples.data(), pcm.constData(), samples.size() * sizeof(float));
    // длинный трек слушаем после вступления, короткий целиком
    const size_t rate = AudioFeatures::kSampleRate;
    if (samples.size() >= (kSkipSeconds + kAnalyzeSeconds / 2) * rate) {
        size_t from = kSkipSeconds * rate;
        size_t to = std::min(samples.size(), from + kAnalyzeSeconds * rate);
        samples = std::vector<float>(samples.begin() + qsizetype(from), samples.begin() + qsizetype(to));
    }
    m_watcher.setFuture(
        QtConcurrent::run([samples = std::move(samples)]() { return AudioFeatures::analyze(samples); }));
}

void SoundLibrary::finish(const AudioFeatures::Sound& sound) {
    Entry e{sound, m_modified};
    auto old = m_entries.constFind(m_key);
    if (old != m_entries.cend() && old->sound.valid) --m_valid;
    if (sound.valid) ++m_valid;
    m_entries.insert(m_key, e);
    m_scaled = false;

    // и неудачный разбор запоминается: тот же файл не мучаем снова
    if (m_db.isOpen()) {
        QJsonArray timbre;
        for (float v : sound.timbre) timbre.append(double(v));
        QSqlQuery q(m_db);
        q.prepare("INSERT OR REPLACE INTO sounds (key, modified, valid, bpm, energy, loudness, onsets, timbre) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        q.addBindValue(m_key);
        q.addBindValue(m_modified);
        q.addBindValue(sound.valid ? 1 : 0);
        q.addBindValue(sound.bpm);
        q.addBindValue(sound.energy);
        q.addBindValue(sound.loudness);
        q.addBindValue(sound.onsetRate);
        q.addBindValue(QString::fromUtf8(QJsonDocument(timbre).toJson(QJsonDocument::Compact)));
        if (!q.exec()) qWarning() << "sound library:" << q.lastError().text();
    }

    QString key = m_key;
    m_busy = false;
    m_key.clear();
    emit analyzed(key);
    if (!m_queue.isEmpty()) QTimer::singleShot(kGapMs, this, &SoundLibrary::next);
}

std::optional<AudioFeatures::Sound> SoundLibrary::sound(const QString& key) const {
    auto it = m_entries.constFind(key);
    if (it == m_entries.cend() || !it->sound.valid) return std::nullopt;
    return it->sound;
}

void SoundLibrary::rescale() const {
    m_mean.fill(0);
    m_spread.fill(0);
    for (const Entry& e : m_entries) {
        if (!e.sound.valid) continue;
        for (int d = 0; d < AudioFeatures::kTimbreDim; ++d) m_mean[d] += e.sound.timbre[d] / float(m_valid);
    }
    for (const Entry& e : m_entries) {
        if (!e.sound.valid) continue;
        for (int d = 0; d < AudioFeatures::kTimbreDim; ++d) {
            float diff = e.sound.timbre[d] - m_mean[d];
            m_spread[d] += diff * diff / float(m_valid);
        }
    }
    for (float& s : m_spread) s = std::max(1e-6F, std::sqrt(s));
    m_scaled = true;
}

std::optional<AudioFeatures::Timbre> SoundLibrary::timbre(const QString& key) const {
    if (m_valid < kMinForScale) return std::nullopt;
    std::optional<AudioFeatures::Sound> s = sound(key);
    if (!s) return std::nullopt;
    if (!m_scaled) rescale();
    AudioFeatures::Timbre z{};
    double length = 0;
    for (int d = 0; d < AudioFeatures::kTimbreDim; ++d) {
        z[d] = (s->timbre[d] - m_mean[d]) / m_spread[d];
        length += double(z[d]) * z[d];
    }
    if (length <= 0) return std::nullopt;
    for (float& v : z) v = float(v / std::sqrt(length));
    return z;
}
