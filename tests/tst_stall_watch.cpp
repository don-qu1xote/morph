#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "utils/StallWatch.h"

// Сторож главного потока: первая запись это старт, потом каждая пауза окна
// длиннее четверти секунды вместе с отмеченными операциями, которые в неё попали
class TestStallWatch : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    QStringList lines() const {
        QFile file(dir.filePath("stalls.log"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    }

private slots:
    void stallIsWrittenWithWhatCausedIt() {
        StallWatch watch(dir.filePath("stalls.log"));
        QTest::qWait(120);
        QCOMPARE(lines().size(), 1);
        QVERIFY(lines().first().contains("start"));

        {
            StallWatch::Scope scope("taste apply");
            QThread::msleep(400);
        }
        QTest::qWait(120);
        QStringList log = lines();
        QCOMPARE(log.size(), 2);
        QVERIFY2(log.last().contains("stall"), qPrintable(log.last()));
        QVERIFY(log.last().contains("taste apply"));

        // короткая работа не пауза, и в журнал не попадает
        {
            StallWatch::Scope scope("rank");
            QThread::msleep(20);
        }
        QTest::qWait(120);
        QCOMPARE(lines().size(), 2);
    }

    // в фоновом потоке отметка ничего не делает: окно её не ждёт
    void scopeOffMainThreadIsSilent() {
        QThread* worker = QThread::create([]() {
            StallWatch::Scope scope("background");
            QThread::msleep(50);
        });
        worker->start();
        QVERIFY(worker->wait(2000));
        delete worker;
        StallWatch watch(dir.filePath("other.log"));
        QTest::qWait(120);
        QFile file(dir.filePath("other.log"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("background"));
    }
};

QTEST_GUILESS_MAIN(TestStallWatch)
#include "tst_stall_watch.moc"
