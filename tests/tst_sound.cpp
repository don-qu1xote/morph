#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <random>

#include "core/AudioFeatures.h"
#include "core/SoundLibrary.h"
#include "support/FakeTools.h"

// Что morph слышит в треке: темп, энергия и тембр по самому сигналу, и разбор
// кэша в фоне. ffmpeg в тесте заменяет скрипт: файл кэша здесь уже сырые
// отсчёты, и скрипт отдаёт их как есть
class TestSound : public QObject {
    Q_OBJECT

    static constexpr int kRate = AudioFeatures::kSampleRate;
    static constexpr double kPi = 3.14159265358979323846;

    FakeTools tools;
    QTemporaryDir dir;

    // бочка на каждой доле, малый на второй и четвёртой, хэт на восьмых
    static std::vector<float> drums(double bpm, double seconds, double gain = 0.5) {
        std::mt19937 rng(1);
        std::normal_distribution<float> noise(0, 1);
        std::vector<float> x(size_t(seconds * kRate), 0);
        const double eighth = 30.0 / bpm;
        for (int k = 0; k * eighth < seconds; ++k) {
            const auto start = size_t(k * eighth * kRate);
            const bool beat = k % 2 == 0;
            const bool snare = beat && (k / 2) % 2 == 1;
            for (int i = 0; i < kRate / 5 && start + i < x.size(); ++i) {
                double t = double(i) / kRate;
                double s = 0.15 * noise(rng) * std::exp(-t * 120);
                if (beat) s += std::sin(2 * kPi * 60 * t) * std::exp(-t * 25);
                if (snare) s += 0.5 * noise(rng) * std::exp(-t * 30);
                x[start + i] += float(gain * s);
            }
        }
        return x;
    }

    // бочка под аккордом и тихим шумом: так ритм выглядит в настоящей записи
    static std::vector<float> song(double bpm, double seconds) {
        std::mt19937 rng(3);
        std::normal_distribution<float> noise(0, 1);
        std::vector<float> x(size_t(seconds * kRate));
        const double beat = 60.0 / bpm;
        for (size_t i = 0; i < x.size(); ++i) {
            double t = double(i) / kRate;
            int chord = int(t / (beat * 8)) % 3;
            double f = chord == 0 ? 220 : (chord == 1 ? 196 : 246.9);
            double phase = std::fmod(t, beat);
            x[i] = float(0.12 * (std::sin(2 * kPi * f * t) + std::sin(2 * kPi * f * 1.26 * t) +
                                 std::sin(2 * kPi * f * 1.5 * t)) +
                         0.02 * noise(rng) + 0.6 * std::sin(2 * kPi * 55 * phase) * std::exp(-phase * 20));
        }
        return x;
    }

    static std::vector<float> tone(double hz, double seconds, double gain) {
        std::vector<float> x(size_t(seconds * kRate));
        for (size_t i = 0; i < x.size(); ++i) x[i] = float(gain * std::sin(2 * kPi * hz * double(i) / kRate));
        return x;
    }

    static std::vector<float> noise(double seconds, double gain, unsigned seed = 2) {
        std::mt19937 rng(seed);
        std::normal_distribution<float> n(0, 1);
        std::vector<float> x(size_t(seconds * kRate));
        for (float& v : x) v = float(gain) * n(rng);
        return x;
    }

    static double distance(const AudioFeatures::Timbre& a, const AudioFeatures::Timbre& b) {
        double sum = 0;
        for (int d = 0; d < AudioFeatures::kTimbreDim; ++d) sum += double(a[d] - b[d]) * (a[d] - b[d]);
        return std::sqrt(sum);
    }

    static double cosine(const AudioFeatures::Timbre& a, const AudioFeatures::Timbre& b) {
        double dot = 0;
        for (int d = 0; d < AudioFeatures::kTimbreDim; ++d) dot += double(a[d]) * b[d];
        return dot;
    }

    QString cacheDir() const { return dir.filePath("cache"); }

    QString writeCached(const QString& name, const std::vector<float>& samples) const {
        QString path = cacheDir() + "/" + name + ".mp3";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable(path));
        file.write(reinterpret_cast<const char*>(samples.data()), qint64(samples.size()) * qint64(sizeof(float)));
        return path;
    }

    int decodes() const {
        QFile log(dir.filePath("ffmpeg.log"));
        if (!log.open(QIODevice::ReadOnly)) return 0;
        return int(log.readAll().count('\n'));
    }

private slots:
    void initTestCase() {
        // отдаёт файл после -i как есть и записывает каждый вызов
        tools.add("ffmpeg", "echo \"$@\" >> \"" + dir.filePath("ffmpeg.log").toUtf8() +
                                "\"\nwhile [ \"$1\" != \"-i\" ]; do shift; done\ncat \"$2\"\n");
    }

    void init() {
        QDir(cacheDir()).removeRecursively();
        QDir().mkpath(cacheDir());
        for (const QString& f : {"sound.db", "sound.db-wal", "sound.db-shm", "ffmpeg.log"})
            QFile::remove(dir.filePath(f));
    }

    void tempoOfDrumsAndSongs() {
        for (double bpm : {90.0, 100.0, 120.0, 128.0}) {
            AudioFeatures::Sound s = AudioFeatures::analyze(drums(bpm, 30));
            QVERIFY(s.valid);
            QVERIFY2(AudioFeatures::tempoDistance(s.bpm, bpm) < 0.02, qPrintable(QString::number(s.bpm)));
        }
        for (double bpm : {85.0, 135.0}) {
            AudioFeatures::Sound s = AudioFeatures::analyze(song(bpm, 30));
            QVERIFY2(AudioFeatures::tempoDistance(s.bpm, bpm) < 0.02, qPrintable(QString::number(s.bpm)));
        }
    }

    // под 70 и под 140 качает одинаково: для перехода это один темп
    void tempoDistanceFoldsOctaves() {
        QCOMPARE(AudioFeatures::tempoDistance(120, 60), 0.0);
        QCOMPARE(AudioFeatures::tempoDistance(70, 140), 0.0);
        QVERIFY(qAbs(AudioFeatures::tempoDistance(120, 138) - std::log2(138.0 / 120)) < 1e-9);
        QCOMPARE(AudioFeatures::tempoDistance(0, 120), 0.5);
    }

    void steadySoundHasNoBeat() {
        QCOMPARE(AudioFeatures::analyze(tone(440, 20, 0.3)).bpm, 0.0);
        QCOMPARE(AudioFeatures::analyze(tone(440, 20, 0.3)).onsetRate, 0.0);
        QCOMPARE(AudioFeatures::analyze(noise(20, 0.3)).bpm, 0.0);
        QVERIFY(!AudioFeatures::analyze(std::vector<float>(size_t(kRate) * 20, 0)).valid);
        QVERIFY(!AudioFeatures::analyze(drums(120, 3)).valid);
    }

    void loudAndBusyIsEnergetic() {
        double loud = AudioFeatures::analyze(drums(140, 20, 0.6)).energy;
        double quiet = AudioFeatures::analyze(drums(140, 20, 0.03)).energy;
        double calm = AudioFeatures::analyze(tone(220, 20, 0.05)).energy;
        QVERIFY2(loud > quiet + 0.2, qPrintable(QString("%1 %2").arg(loud).arg(quiet)));
        QVERIFY2(quiet > calm, qPrintable(QString("%1 %2").arg(quiet).arg(calm)));
    }

    void timbreTellsToneFromNoise() {
        AudioFeatures::Timbre a = AudioFeatures::analyze(tone(440, 20, 0.3)).timbre;
        AudioFeatures::Timbre b = AudioFeatures::analyze(tone(466, 20, 0.3)).timbre;
        AudioFeatures::Timbre c = AudioFeatures::analyze(noise(20, 0.3)).timbre;
        QVERIFY(distance(a, b) < distance(a, c));
    }

    // Кэш разбирается в фоне по одному файлу. Неудачный разбор тоже
    // запоминается, после перезапуска всё берётся из базы, а другой файл под
    // тем же именем разбирается заново
    void cacheIsHeardInBackground() {
        writeCached("120", drums(120, 40));
        writeCached("90", drums(90, 40));
        writeCached("quiet", std::vector<float>(size_t(kRate) * 40, 0));
        {
            SoundLibrary sounds(dir.filePath("sound.db"));
            sounds.setDecoder(tools.path("ffmpeg"));
            sounds.scan(cacheDir());
            QCOMPARE(sounds.pending(), 3);
            QTRY_COMPARE_WITH_TIMEOUT(sounds.pending(), 0, 30000);
            QCOMPARE(sounds.size(), 2);
            QVERIFY(AudioFeatures::tempoDistance(sounds.sound("120").value_or(AudioFeatures::Sound{}).bpm, 120) < 0.02);
            QVERIFY(AudioFeatures::tempoDistance(sounds.sound("90").value_or(AudioFeatures::Sound{}).bpm, 90) < 0.02);
            QVERIFY(!sounds.sound("quiet").has_value());
            QCOMPARE(decodes(), 3);
        }

        SoundLibrary again(dir.filePath("sound.db"));
        again.setDecoder(tools.path("ffmpeg"));
        again.scan(cacheDir());
        QCOMPARE(again.pending(), 0);
        QCOMPARE(again.size(), 2);

        QString path = writeCached("90", drums(100, 40));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
        file.close();
        again.enqueue("90", path);
        QTRY_COMPARE_WITH_TIMEOUT(again.pending(), 0, 30000);
        QCOMPARE(decodes(), 4);
        QVERIFY(AudioFeatures::tempoDistance(again.sound("90").value_or(AudioFeatures::Sound{}).bpm, 100) < 0.02);
    }

    void noDecoderNoWork() {
        writeCached("120", drums(120, 10));
        SoundLibrary sounds(dir.filePath("sound.db"));
        sounds.scan(cacheDir());
        QVERIFY(!sounds.hasDecoder());
        QCOMPARE(sounds.pending(), 0);
    }

    // Тембр сравнивается в разбросах от среднего по библиотеке. Пока треков
    // мало, среднее ненадёжно и тембра нет; потом шум ближе к шуму, чем к тону
    void timbreNeedsEnoughTracks() {
        SoundLibrary sounds(dir.filePath("sound.db"));
        sounds.setDecoder(tools.path("ffmpeg"));
        for (int i = 0; i < SoundLibrary::kMinForScale - 1; ++i) {
            QString name = QString("t%1").arg(i);
            sounds.enqueue(name, writeCached(name, i % 2 ? tone(300 + 20 * i, 6, 0.3) : noise(6, 0.3, i)));
        }
        QTRY_COMPARE_WITH_TIMEOUT(sounds.pending(), 0, 60000);
        QVERIFY(!sounds.timbre("t0").has_value());

        QString last = QString("t%1").arg(SoundLibrary::kMinForScale - 1);
        sounds.enqueue(last, writeCached(last, tone(700, 6, 0.3)));
        QTRY_COMPARE_WITH_TIMEOUT(sounds.pending(), 0, 30000);
        AudioFeatures::Timbre noise0 = sounds.timbre("t0").value_or(AudioFeatures::Timbre{});
        AudioFeatures::Timbre noise2 = sounds.timbre("t2").value_or(AudioFeatures::Timbre{});
        AudioFeatures::Timbre tone1 = sounds.timbre("t1").value_or(AudioFeatures::Timbre{});
        QVERIFY(qAbs(cosine(noise0, noise0) - 1) < 1e-5);
        QVERIFY(cosine(noise0, noise2) > cosine(noise0, tone1));
    }
};

QTEST_GUILESS_MAIN(TestSound)
#include "tst_sound.moc"
