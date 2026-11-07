#include "StallWatch.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QStringList>
#include <QThread>
#include <algorithm>

namespace {

const int kTickMs = 50;
// журнал больше этого начинается заново: нужны свежие паузы, а не вся жизнь
const qint64 kMaxLogBytes = 256 * 1024;
// отметки старше этого уже ни с какой паузой не пересекутся
const qint64 kKeepMs = 10000;

struct Mark {
    const char* what;
    qint64 start;
    qint64 duration;
};

QElapsedTimer& monotonic() {
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer;
}

// только главный поток: и отметки, и сторож живут в нём
QList<Mark>& marks() {
    static QList<Mark> list;
    return list;
}

bool onMainThread() {
    return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
}

} // namespace

StallWatch::Scope::Scope(const char* what) {
    if (!onMainThread()) return;
    m_what = what;
    m_start = monotonic().elapsed();
}

StallWatch::Scope::~Scope() {
    if (!m_what) return;
    qint64 duration = monotonic().elapsed() - m_start;
    if (duration >= kMarkMs) marks().append({m_what, m_start, duration});
}

StallWatch::StallWatch(const QString& logPath, QObject* parent) : QObject(parent), m_path(logPath) {
    m_last = monotonic().elapsed();
    connect(&m_timer, &QTimer::timeout, this, &StallWatch::tick);
    m_timer.start(kTickMs);
}

void StallWatch::tick() {
    const qint64 now = monotonic().elapsed();
    const qint64 late = now - m_last - kTickMs;
    const qint64 from = m_first ? 0 : m_last;
    QList<Mark>& list = marks();
    if (late > kStallMs || m_first) {
        QStringList during;
        for (const Mark& m : std::as_const(list)) {
            if (m.start + m.duration >= from && m.start <= now)
                during.append(QString("%1 %2 ms").arg(QString::fromLatin1(m.what)).arg(m.duration));
        }
        // старт пишется всегда: по нему видно, быстрее ли стало после правок
        QString what = m_first ? QString("start %1 ms").arg(now) : QString("stall %1 ms").arg(late);
        write(QString("%1  %2  during: %3")
                  .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), what,
                       during.isEmpty() ? QString("unmarked (QML or Qt)") : during.join(", ")));
    }
    m_first = false;
    m_last = now;
    list.erase(std::remove_if(list.begin(), list.end(), [now](const Mark& m) { return now - m.start > kKeepMs; }),
               list.end());
}

void StallWatch::write(const QString& line) const {
    if (QFile::exists(m_path) && QFile(m_path).size() > kMaxLogBytes) QFile::remove(m_path);
    QFile file(m_path);
    if (file.open(QIODevice::Append | QIODevice::Text)) file.write(line.toUtf8() + "\n");
}
