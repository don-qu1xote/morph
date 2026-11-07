#include <QTest>

#include "core/Evaluation.h"
#include "core/MusicSpace.h"
#include "core/TrackMatch.h"

// Проверка модели на истории: модель угадывает, потом учится, и в проверку
// идут только последние события. Главное, ради чего это всё: видно, даёт ли
// карта музыки что-то сверх истории
class TestEvaluation : public QObject {
    Q_OBJECT

    static QVariantMap track(const QString& artist, int n, const QString& via = {}) {
        QVariantMap t{{"id", artist + QString::number(n)},
                      {"service", "Yandex"},
                      {"artist", artist},
                      {"title", "Song " + QString::number(n)},
                      {"durationMs", 200000}};
        if (!via.isEmpty()) t["via"] = via;
        return t;
    }

    static ListenEvent listen(const QVariantMap& t, qint64 at, bool finished) {
        ListenEvent e;
        e.track = t;
        e.at = at;
        e.length = 200;
        e.played = finished ? 200 : 10;
        e.completed = finished;
        return e;
    }

    static QStringList crowd(const QString& name) {
        QStringList out;
        for (int i = 1; i <= 20; ++i) out.append(name + " " + QString::number(i));
        return out;
    }

    // две компании по двадцать артистов, внутри все похожи
    static MusicSpace crowds() {
        QList<QPair<QString, QList<KnowledgeBase::Neighbor>>> g;
        for (const QStringList& members : {crowd("Rap"), crowd("Trip")}) {
            for (const QString& artist : members) {
                QList<KnowledgeBase::Neighbor> n;
                for (const QString& other : members) {
                    if (other != artist) n.append({other, {}, 0.8, 0});
                }
                g.append({artist, n});
            }
        }
        return MusicSpace::build(g);
    }

    // Учится на двух рэперах (дослушивает) и двух трип-хоп артистах
    // (пропускает). Проверка на остальных, каждый звучит один раз и пришёл из
    // Last.fm: ни артист, ни источник не подсказывают ответ, угадать можно
    // только по карте. Повтор артиста в проверке подсказал бы: после первого
    // раза модель его уже знает
    static QList<ListenEvent> history() {
        QList<ListenEvent> events;
        qint64 at = 1'800'000'000;
        const QStringList rap = crowd("Rap");
        const QStringList trip = crowd("Trip");
        for (int i = 0; i < 64; ++i) {
            events.append(listen(track(rap[i % 2], i % 7), at, true));
            at += 600;
            events.append(listen(track(trip[i % 2], i % 7), at, false));
            at += 600;
        }
        for (int i = 2; i < 18; ++i) {
            events.append(listen(track(rap[i], 1, "Last.fm"), at, true));
            at += 600;
            events.append(listen(track(trip[i], 1, "Last.fm"), at, false));
            at += 600;
        }
        return events;
    }

private slots:
    void aucCountsPairs() {
        QCOMPARE(Evaluation::auc({{0.9, true}, {0.8, true}, {0.2, false}}), 1.0);
        QCOMPARE(Evaluation::auc({{0.1, true}, {0.8, false}}), 0.0);
        QCOMPARE(Evaluation::auc({{0.5, true}, {0.5, false}}), 0.5);
        QCOMPARE(Evaluation::auc({{0.5, true}}), 0.5);
        // две пары из четырёх в пользу дослушанного, одна ничья: (2 + 0.5) / 4
        QCOMPARE(Evaluation::auc({{0.9, true}, {0.4, true}, {0.6, false}, {0.4, false}}), 0.625);
    }

    void mapGuessesArtistsNeverHeard() {
        const QList<ListenEvent> events = history();
        MusicSpace space = crowds();
        Evaluation::Setup withMap;
        withMap.vectorOf = [&space](const QVariantMap& t) { return space.song(t, {}); };
        // в маленькой истории меньше сотни разных песен: соперников двадцать
        withMap.negatives = 20;
        Evaluation::Setup plain;
        plain.negatives = 20;

        Evaluation::Report full = Evaluation::run(events, withMap);
        Evaluation::Report base = Evaluation::run(events, plain);
        QCOMPARE(full.events, 160);
        QCOMPARE(full.scored, 32);
        QCOMPARE(full.testFrom, events[128].at);
        QVERIFY2(full.auc > 0.9, qPrintable(QString::number(full.auc)));
        QVERIFY2(base.auc < 0.6, qPrintable(QString::number(base.auc)));
        QCOMPARE(full.skips["Last.fm"], qMakePair(16, 32));
        QVERIFY(!full.skips.contains("Yandex"));
        // дослушанный новый рэпер среди двадцати уже звучавших песен
        QCOMPARE(full.ranked, 16);
        QVERIFY(full.hitRate >= 0 && full.hitRate <= 1);
        QVERIFY(full.ndcg <= full.hitRate);
    }

    // прогон ничего не пишет на диск и даёт одно и то же при одном и том же входе
    void sameHistorySameNumbers() {
        const QList<ListenEvent> events = history();
        Evaluation::Setup setup;
        Evaluation::Report a = Evaluation::run(events, setup);
        Evaluation::Report b = Evaluation::run(events, setup);
        QCOMPARE(a.auc, b.auc);
        QCOMPARE(a.hitRate, b.hitRate);
    }
};

QTEST_GUILESS_MAIN(TestEvaluation)
#include "tst_evaluation.moc"
