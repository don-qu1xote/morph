#ifndef SOUND_LIBRARY_H
#define SOUND_LIBRARY_H

#include "AudioFeatures.h"
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSqlDatabase>
#include <optional>

class QProcess;

// Звук каждого трека из кэша: темп, энергия и тембр (AudioFeatures). ffmpeg
// раскладывает файл в моно 22050 Гц, анализ идёт в фоновом потоке по одному
// файлу с паузой, чтобы не отнимать процессор у музыки. Берётся минута с 30-й
// секунды: вступление часто тише и реже остального трека. Результат лежит в
// sound.db и переживает очистку кэша: звук трека от этого не меняется
class SoundLibrary : public QObject {
    Q_OBJECT
public:
    explicit SoundLibrary(const QString& dbPath, QObject* parent = nullptr);
    ~SoundLibrary() override;

    // путь к ffmpeg; пусто, если его нет, тогда и анализа нет
    void setDecoder(const QString& ffmpeg) { m_ffmpeg = ffmpeg; }
    bool hasDecoder() const { return !m_ffmpeg.isEmpty(); }

    // key это имя файла в кэше без расширения. Разобранный и с тех пор не
    // изменившийся файл второй раз не разбирается
    void enqueue(const QString& key, const QString& path);
    // все файлы папки кэша, которых ещё нет
    void scan(const QString& dir);

    std::optional<AudioFeatures::Sound> sound(const QString& key) const;
    // Тембр в общих единицах: каждое измерение в разбросах от среднего по
    // всей библиотеке, длина 1. Без этого MFCC с большими числами заглушали
    // бы остальные. Пока разобрано меньше kMinForScale треков, среднее
    // ненадёжно, и тембра нет
    std::optional<AudioFeatures::Timbre> timbre(const QString& key) const;
    static constexpr int kMinForScale = 20;

    // с годным звуком
    int size() const { return m_valid; }
    // в очереди вместе с тем, что разбирается сейчас
    int pending() const { return int(m_queue.size()) + (m_busy ? 1 : 0); }

signals:
    void analyzed(const QString& key);

private:
    struct Entry {
        AudioFeatures::Sound sound;
        // время изменения файла: другой файл под тем же id разбирается заново
        qint64 modified = 0;
    };
    void next();
    void decoded(const QByteArray& pcm);
    void finish(const AudioFeatures::Sound& sound);
    void rescale() const;

    QString m_ffmpeg;
    QString m_connection;
    QSqlDatabase m_db;
    QHash<QString, Entry> m_entries;
    int m_valid = 0;

    QList<QPair<QString, QString>> m_queue;
    bool m_busy = false;
    QString m_key;
    qint64 m_modified = 0;
    QProcess* m_process = nullptr;
    QFutureWatcher<AudioFeatures::Sound> m_watcher;

    mutable bool m_scaled = false;
    mutable AudioFeatures::Timbre m_mean{};
    mutable AudioFeatures::Timbre m_spread{};
};

#endif
