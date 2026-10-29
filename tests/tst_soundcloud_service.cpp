#include <QDateTime>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

#include "services/SoundCloudService.h"
#include "support/FakeHttpServer.h"

class TestSoundCloudService : public QObject {
    Q_OBJECT

    FakeHttpServer api;

    // подпись не проверяется ни нами, ни тестом, важны только поля внутри
    static QString jwt(const QString& clientId, qint64 exp) {
        auto part = [](const QJsonObject& o) {
            return QString::fromLatin1(QJsonDocument(o)
                                           .toJson(QJsonDocument::Compact)
                                           .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        };
        return part({{"alg", "RS256"}}) + "." + part({{"client_id", clientId}, {"exp", exp}}) + ".signature";
    }

    HttpRequest searchWith(const QString& token) {
        api.requests.clear();
        NetworkManager net;
        SoundCloudService soundcloud(&net);
        soundcloud.setToken(token);
        QSignalSpy results(&soundcloud, &BaseService::searchResultsReady);
        soundcloud.search("song");
        if (!results.wait()) return {};
        return api.requestsTo("/search/tracks").value(0);
    }

private slots:
    void initTestCase() {
        qputenv("MORPH_SOUNDCLOUD_API", api.url().toUtf8());
        api.on("GET", "/search/tracks", [](const HttpRequest&) { return jsonBody({{"collection", QJsonArray{}}}); });
    }

    void plainClientId() {
        HttpRequest r = searchWith("QhVEWrrJKvW4uSU5u5EoI0FTXYUGzy5k");
        QCOMPARE(r.query.queryItemValue("client_id"), QString("QhVEWrrJKvW4uSU5u5EoI0FTXYUGzy5k"));
        QVERIFY(!r.headers.contains("authorization"));
    }

    void legacyOAuthToken() {
        HttpRequest r = searchWith("2-123456-abc");
        QVERIFY(!r.query.hasQueryItem("client_id"));
        QCOMPARE(r.headers["authorization"], QByteArray("OAuth 2-123456-abc"));
    }

    void freshJwtGivesClientIdAndAuth() {
        QString token = jwt("public-id", QDateTime::currentSecsSinceEpoch() + 3600);
        HttpRequest r = searchWith(token);
        QCOMPARE(r.query.queryItemValue("client_id"), QString("public-id"));
        QCOMPARE(r.headers["authorization"], ("OAuth " + token).toUtf8());
    }

    void expiredJwtStillGivesClientId() {
        HttpRequest r = searchWith(jwt("public-id", QDateTime::currentSecsSinceEpoch() - 10));
        QCOMPARE(r.query.queryItemValue("client_id"), QString("public-id"));
        QVERIFY(!r.headers.contains("authorization"));
    }

    // из DevTools копируют строку заголовка целиком, вместе с "OAuth "
    void wholeHeaderLineIsAccepted() {
        QString token = jwt("public-id", QDateTime::currentSecsSinceEpoch() + 3600);
        HttpRequest r = searchWith("  OAuth " + token + "\n");
        QCOMPARE(r.query.queryItemValue("client_id"), QString("public-id"));
        QCOMPARE(r.headers["authorization"], ("OAuth " + token).toUtf8());
    }
};

QTEST_GUILESS_MAIN(TestSoundCloudService)
#include "tst_soundcloud_service.moc"
