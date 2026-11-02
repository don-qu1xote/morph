#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "services/PinterestCovers.h"
#include "support/FakeHttpServer.h"

// Обложки с доски Pinterest: доска находится по ссылке или имени, картинки не
// повторяются, пока доска не кончится
class TestPinterestCovers : public QObject {
    Q_OBJECT

    FakeHttpServer api;
    QTemporaryDir dir;

    static QJsonObject pin(const QString& size, const QString& url) {
        return {{"id", url}, {"media", QJsonObject{{"images", QJsonObject{{size, QJsonObject{{"url", url}}}}}}}};
    }

    void serveBoard() {
        api.on("GET", "/boards", [](const HttpRequest& r) {
            if (r.headers["authorization"] != "Bearer tok") return HttpResponse{401, "{}"};
            return jsonBody({{"items", QJsonArray{QJsonObject{{"id", "b0"}, {"name", "Other"}},
                                                  QJsonObject{{"id", "1001"}, {"name", "Morph Covers"}}}}});
        });
        api.on("GET", "/boards/1001/pins", [](const HttpRequest& r) {
            if (r.query.queryItemValue("bookmark") == "p2")
                return jsonBody({{"items", QJsonArray{pin("600x", "https://i/3")}}});
            return jsonBody({{"items", QJsonArray{pin("1200x", "https://i/1"), pin("600x", "https://i/2"),
                                                  QJsonObject{{"id", "video"}}}},
                             {"bookmark", "p2"}});
        });
    }

    QString statePath() const { return dir.filePath("pinterest.json"); }

private slots:
    void initTestCase() { qputenv("MORPH_PINTEREST_API", api.url().toUtf8()); }

    void init() {
        api.reset();
        QFile::remove(statePath());
    }

    void boardByLinkAndNoRepeats() {
        serveBoard();
        NetworkManager net;
        PinterestCovers covers(&net, statePath());
        QSignalSpy loaded(&covers, &PinterestCovers::loaded);
        QVERIFY(covers.next("a").isEmpty());

        covers.setAccount("Bearer tok", "https://www.pinterest.com/melidadr/morph-covers/");
        QVERIFY(loaded.wait());
        QCOMPARE(loaded[0][0].toInt(), 3);
        QCOMPARE(api.requestsTo("/boards/1001/pins").size(), 2);

        QSet<QString> seen;
        for (int i = 0; i < 3; ++i) seen.insert(covers.next(i % 2 ? "a" : "b"));
        QCOMPARE(seen.size(), 3);

        // доска кончилась: новый круг, но не с той же картинкой, что у плейлиста сейчас
        QString before = covers.current("a");
        QString after = covers.next("a");
        QVERIFY(!after.isEmpty());
        QVERIFY(after != before);

        // уже выданные картинки помнятся и после перезапуска
        PinterestCovers restarted(&net, statePath());
        QVERIFY(restarted.isOurs(after));
        QVERIFY(!restarted.isOurs("https://imgur.example/mine.png"));
    }

    void boardByIdSkipsTheList() {
        serveBoard();
        NetworkManager net;
        PinterestCovers covers(&net, statePath());
        QSignalSpy loaded(&covers, &PinterestCovers::loaded);
        covers.setAccount("tok", "1001");
        QVERIFY(loaded.wait());
        QVERIFY(api.requestsTo("/boards").isEmpty());
    }

    void problemsAreSaidOutLoud() {
        serveBoard();
        NetworkManager net;
        PinterestCovers covers(&net, statePath());
        QSignalSpy failed(&covers, &PinterestCovers::failed);

        covers.setAccount("wrong", "Morph Covers");
        QVERIFY(failed.wait());
        QVERIFY(failed[0][0].toString().contains("token"));

        covers.setAccount("tok", "No Such Board");
        QVERIFY(failed.wait());
        QVERIFY(failed[1][0].toString().contains("not found"));
        QVERIFY(!covers.ready());
    }
};

QTEST_GUILESS_MAIN(TestPinterestCovers)
#include "tst_pinterest_covers.moc"
