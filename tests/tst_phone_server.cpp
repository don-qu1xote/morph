#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/ServiceManager.h"
#include "server/PhoneMixes.h"
#include "server/ServerAudio.h"
#include "support/FakeHttpServer.h"
#include "support/FakeTools.h"
#include "utils/BackgroundWriter.h"
#include "utils/PathProvider.h"

// Файлы и миксы для телефона: что отдаётся из кэша, что сначала качается,
// что перекладывается ffmpeg, и как миксы обновляются раз в день
class TestPhoneServer : public QObject {
    Q_OBJECT

    FakeHttpServer soundcloudApi;
    FakeTools tools;
    QTemporaryDir dir;
    QString ffmpeg;

    static QByteArray mp3Bytes() {
        QByteArray bytes = "ID3";
        bytes.append(QByteArray(20000, '\x01'));
        return bytes;
    }

    static void write(const QString& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

    static QByteArray box(const QByteArray& type, int payload) {
        QByteArray b(4, '\0');
        int size = 8 + payload;
        b[0] = char(size >> 24);
        b[1] = char(size >> 16);
        b[2] = char(size >> 8);
        b[3] = char(size);
        return b + type + QByteArray(payload, '\0');
    }

    // ffmpeg из системы: тест сам делает им файлы, которые потом перекладывает morph
    bool makeAudio(const QStringList& outputArgs, const QString& path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QProcess p;
        p.start(ffmpeg,
                QStringList{"-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=2"} +
                    outputArgs + QStringList{path});
        return p.waitForFinished(30000) && p.exitCode() == 0;
    }

    struct Served {
        QString path;
        QByteArray type;
        bool done = false;
    };

    static Served fetchNow(ServerAudio& audio, const QVariantMap& track, int timeoutMs = 10000) {
        auto served = std::make_shared<Served>();
        audio.fetch(track, [served](const QString& path, const QByteArray& type) {
            served->path = path;
            served->type = type;
            served->done = true;
        });
        if (!QTest::qWaitFor([&] { return served->done; }, timeoutMs)) qWarning("fetch timed out");
        return *served;
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("MORPH_SOUNDCLOUD_API", soundcloudApi.url().toUtf8());
        tools.add("yt-dlp", "exit 0\n");
        ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        QDir(PathProvider::getTrackCachePath()).removeRecursively();
        QDir().mkpath(PathProvider::getTrackCachePath());
    }

    void sniffKnowsWhatPhoneCanPlay() {
        auto sniffed = [this](const QString& name, const QByteArray& bytes) {
            write(dir.filePath(name), bytes);
            return ServerAudio::sniff(dir.filePath(name));
        };
        QCOMPARE(sniffed("id3", mp3Bytes()).contentType, QByteArray("audio/mpeg"));
        QCOMPARE(sniffed("mp3", QByteArray("\xFF\xFB\x90\x64") + QByteArray(600, '\0')).suffix, QString("mp3"));
        QCOMPARE(sniffed("adts", QByteArray("\xFF\xF1\x50\x80") + QByteArray(600, '\0')).contentType,
                 QByteArray("audio/aac"));

        ServerAudio::Format plain = sniffed("plain.m4a", box("ftyp", 16) + box("moov", 100) + box("mdat", 4000));
        QCOMPARE(plain.contentType, QByteArray("audio/mp4"));
        QVERIFY(plain.playable);
        QVERIFY(!plain.fragmented);
        ServerAudio::Format pieces =
            sniffed("frag.m4a", box("ftyp", 16) + box("moov", 100) + box("moof", 50) + box("mdat", 4000));
        QVERIFY(pieces.fragmented);

        QVERIFY(!sniffed("ogg", "OggS" + QByteArray(600, '\0')).playable);
        QVERIFY(!sniffed("webm", QByteArray("\x1A\x45\xDF\xA3") + QByteArray(600, '\0')).playable);
        QByteArray ts(600, '\0');
        ts[0] = 0x47;
        ts[188] = 0x47;
        QCOMPARE(sniffed("ts", ts).suffix, QString("ts"));
        QCOMPARE(sniffed("junk", QByteArray(600, 'x')).playable, false);
    }

    void cachedTrackIsServedAsIs() {
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        ServerAudio audio(&services, &cache, dir.filePath("phone_audio"));
        write(cache.getTrackPath("y1"), mp3Bytes());

        QVariantMap track{{"service", "Yandex"}, {"id", "y1"}};
        Served served = fetchNow(audio, track);
        QCOMPARE(served.path, cache.getTrackPath("y1"));
        QCOMPARE(served.type, QByteArray("audio/mpeg"));
        SubsonicBackend::AudioInfo info = audio.info(track);
        QCOMPARE(info.contentType, QByteArray("audio/mpeg"));
        QCOMPARE(info.size, qint64(mp3Bytes().size()));
        // нескачанный трек: тип угадывается по площадке
        QCOMPARE(audio.info({{"service", "SoundCloud"}, {"id", "nope"}}).suffix, QString("m4a"));
    }

    // Трека нет в кэше: ищется как для окна, но окно об этом не слышит,
    // иначе его текущий трек начался бы заново
    void missingTrackIsDownloadedFirstAndWindowStaysQuiet() {
        QString base = soundcloudApi.url();
        soundcloudApi.on("GET", "/tracks/42", [base](const HttpRequest&) {
            QJsonObject transcoding{{"url", base + "/media/42"}, {"format", QJsonObject{{"protocol", "progressive"}}}};
            return jsonBody({{"media", QJsonObject{{"transcodings", QJsonArray{transcoding}}}}});
        });
        soundcloudApi.on("GET", "/media/42",
                         [base](const HttpRequest&) { return jsonBody({{"url", base + "/file/42.mp3"}}); });
        soundcloudApi.on("GET", "/file/42.mp3", [](const HttpRequest&) { return HttpResponse{200, mp3Bytes()}; });

        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setSoundCloudClientId("client");
        ServerAudio audio(&services, &cache, dir.filePath("phone_audio"));
        QSignalSpy window(&services, &ServiceManager::streamUrlReady);

        Served served = fetchNow(audio, {{"service", "SoundCloud"}, {"id", "42"}});
        QCOMPARE(served.path, cache.getTrackPath("42"));
        QFile file(served.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll() == mp3Bytes());
        QCOMPARE(window.count(), 0);

        // а то, что просит окно, приходит окну, как раньше
        QSignalSpy phone(&services, &ServiceManager::phoneStreamReady);
        cache.forgetTrack("42");
        services.resolve("SoundCloud", "42");
        QVERIFY(window.wait());
        QCOMPARE(phone.count(), 0);
    }

    void unknownTrackFailsAndIsRemembered() {
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        ServerAudio audio(&services, &cache, dir.filePath("phone_audio"));
        QSignalSpy failed(&services, &ServiceManager::phoneStreamFailed);

        QVERIFY(fetchNow(audio, {{"service", "Nope"}, {"id", "x1"}}).path.isEmpty());
        QCOMPARE(failed.count(), 1);
        QVERIFY(fetchNow(audio, {{"service", "Nope"}, {"id", "x1"}}).path.isEmpty());
        QCOMPARE(failed.count(), 1);
    }

    void unplayableFilesBecomeM4a() {
        if (ffmpeg.isEmpty()) QSKIP("ffmpeg is not installed");
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        ServerAudio audio(&services, &cache, dir.filePath("phone_audio"));

        // MPEG-TS с AAC, как склеенный HLS, и MP4 кусками, как отдаёт YouTube
        QVERIFY(makeAudio({"-c:a", "aac", "-f", "mpegts"}, cache.getTrackPath("ts1")));
        QVERIFY(makeAudio({"-c:a", "aac", "-movflags", "frag_keyframe+empty_moov", "-f", "mp4"},
                          cache.getTrackPath("frag1")));
        QVERIFY(ServerAudio::sniff(cache.getTrackPath("frag1")).fragmented);

        for (const QString& id : {QString("ts1"), QString("frag1")}) {
            Served served = fetchNow(audio, {{"service", "YouTube"}, {"id", id}}, 30000);
            QVERIFY2(served.path.endsWith(id + ".m4a"), qPrintable(served.path));
            QCOMPARE(served.type, QByteArray("audio/mp4"));
            ServerAudio::Format f = ServerAudio::sniff(served.path);
            QCOMPARE(f.contentType, QByteArray("audio/mp4"));
            QVERIFY(!f.fragmented);
            QCOMPARE(audio.info({{"service", "YouTube"}, {"id", id}}).contentType, QByteArray("audio/mp4"));
        }
        // второй раз готовая копия, без нового ffmpeg
        QDateTime before = QFileInfo(dir.filePath("phone_audio/ts1.m4a")).lastModified();
        QTest::qWait(20);
        QCOMPARE(fetchNow(audio, {{"service", "YouTube"}, {"id", "ts1"}}).path, dir.filePath("phone_audio/ts1.m4a"));
        QCOMPARE(QFileInfo(dir.filePath("phone_audio/ts1.m4a")).lastModified(), before);
    }

    void prefetchGoesOneByOne() {
        NetworkManager net;
        CacheManager cache(&net);
        ServiceManager services(&net, &cache);
        services.setSoundCloudClientId("client");
        ServerAudio audio(&services, &cache, dir.filePath("phone_audio"));
        cache.forgetTrack("42");
        write(cache.getTrackPath("y1"), mp3Bytes());

        audio.prefetch({QVariantMap{{"service", "Yandex"}, {"id", "y1"}},
                        QVariantMap{{"service", "SoundCloud"}, {"id", "42"}},
                        QVariantMap{{"service", "Yandex"}, {"id", "y1"}}});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(cache.getTrackPath("42")).size() > 4096, 10000);
    }

    // Круг сборки: Daily Mix каждый день, недельные раз в неделю, On Repeat
    // телефону не нужен. Ответы приходят и сразу, и позже, как у MorphMixes
    void mixesAreBuiltOncePerDay() {
        QString path = dir.filePath("phone_mixes.json");
        QFile::remove(path);
        QDateTime now(QDate(2026, 11, 6), QTime(9, 0));
        QStringList asked;
        PhoneMixes* mixesPtr = nullptr;
        auto cards = [] {
            QVariantList out;
            for (const char* id :
                 {"morph:daily1", "morph:daily2", "morph:discover", "morph:onrepeat", "morph:daily3"}) {
                out.append(QVariantMap{{"id", id}});
            }
            return out;
        };
        auto tracks = [](int n, const QString& prefix) {
            QVariantList out;
            for (int i = 0; i < n; ++i)
                out.append(QVariantMap{{"service", "Yandex"}, {"id", prefix + QString::number(i)}});
            return out;
        };
        auto build = [&](const QString& id) {
            asked.append(id);
            if (id == "morph:daily1") mixesPtr->collected("Daily Mix 1", "", tracks(40, "a"));
            else if (id == "morph:daily3") mixesPtr->skipped();
            else
                QTimer::singleShot(10, mixesPtr, [=] {
                    mixesPtr->collected(id == "morph:daily2" ? "Daily Mix 2" : "Discover Weekly", "", tracks(5, id));
                });
        };

        auto mixes = std::make_unique<PhoneMixes>(cards, build, path);
        mixesPtr = mixes.get();
        mixes->setClock([&] { return now; });
        QSignalSpy built(mixes.get(), &PhoneMixes::built);
        mixes->check();
        QVERIFY(built.wait());
        QCOMPARE(asked, QStringList({"morph:daily1", "morph:daily2", "morph:discover", "morph:daily3"}));
        QVariantList list = mixes->mixes();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].toMap()["name"].toString(), QString("Daily Mix 1, Nov 6"));
        QCOMPARE(list[0].toMap()["tracks"].toList().size(), PhoneMixes::kTracksPerMix);
        QCOMPARE(list[2].toMap()["name"].toString(), QString("Discover Weekly, Nov 6"));
        QVERIFY(list[0].toMap()["created"].toLongLong() > list[1].toMap()["created"].toLongLong());
        QCOMPARE(built[0][0].toList().size(), 40);

        // тот же день: ничего не собирается, и после перезапуска тоже
        asked.clear();
        mixes->check();
        mixes.reset();
        BackgroundWriter::waitForAll();
        mixes = std::make_unique<PhoneMixes>(cards, build, path);
        mixesPtr = mixes.get();
        mixes->setClock([&] { return now; });
        mixes->check();
        QTest::qWait(50);
        QVERIFY(asked.isEmpty());
        QCOMPARE(mixes->mixes().size(), 3);

        // завтра новые Daily Mix, Discover Weekly остаётся прежним до новой недели
        now = now.addDays(1);
        QSignalSpy builtAgain(mixes.get(), &PhoneMixes::built);
        mixes->check();
        QVERIFY(builtAgain.wait());
        QCOMPARE(asked, QStringList({"morph:daily1", "morph:daily2", "morph:daily3"}));
        QCOMPARE(mixes->mixes().size(), 5);
        QCOMPARE(mixes->mixes()[0].toMap()["name"].toString(), QString("Daily Mix 1, Nov 7"));

        // через четыре дня вчерашние и позавчерашние Daily Mix уже не нужны
        now = now.addDays(4);
        asked.clear();
        QSignalSpy later(mixes.get(), &PhoneMixes::built);
        mixes->check();
        QVERIFY(later.wait());
        QStringList names;
        for (const QVariant& m : mixes->mixes()) names.append(m.toMap()["name"].toString());
        QCOMPARE(names, QStringList({"Daily Mix 1, Nov 11", "Daily Mix 2, Nov 11", "Discover Weekly, Nov 11"}));
    }
};

QTEST_GUILESS_MAIN(TestPhoneServer)
#include "tst_phone_server.moc"
