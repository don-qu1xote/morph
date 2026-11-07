#include "PhoneMixes.h"
#include "../utils/BackgroundWriter.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSet>

// раз в полчаса проверка, не наступил ли новый день
static const int kCheckMs = 30 * 60 * 1000;
// микс, который за две минуты не собрался, пропускается до завтра
static const int kBuildTimeoutMs = 120000;
static const qint64 kDayMs = 24LL * 3600 * 1000;

static bool isWeekly(const QString& card) {
    return card == "morph:discover" || card == "morph:releases";
}

PhoneMixes::PhoneMixes(std::function<QVariantList()> cards, std::function<void(const QString&)> build,
                       const QString& path, QObject* parent)
    : QObject(parent), m_cards(std::move(cards)), m_build(std::move(build)), m_path(path),
      m_clock([] { return QDateTime::currentDateTime(); }) {
    BackgroundWriter::waitForAll();
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        m_day = root["day"].toString();
        m_week = root["week"].toString();
        m_mixes = root["mixes"].toArray().toVariantList();
    }
    m_timer.setInterval(kCheckMs);
    connect(&m_timer, &QTimer::timeout, this, &PhoneMixes::check);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kBuildTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, &PhoneMixes::skipped);
}

PhoneMixes::~PhoneMixes() {
    BackgroundWriter::waitForAll();
}

void PhoneMixes::setEnabled(bool enabled) {
    if (!enabled) {
        m_timer.stop();
        return;
    }
    m_timer.start();
    // после старта morph истории и MAIN нужно чуть времени, чтобы загрузиться
    QTimer::singleShot(5000, this, &PhoneMixes::check);
}

void PhoneMixes::check() {
    if (!m_building.isEmpty() || !m_queue.isEmpty()) return;
    QDate today = m_clock().date();
    QString day = today.toString(Qt::ISODate);
    if (day == m_day) return;
    int weekYear = 0;
    int week = today.weekNumber(&weekYear);
    m_roundDay = day;
    m_roundWeek = QString("%1-W%2").arg(weekYear).arg(week);
    m_roundWeekly = false;
    m_fresh.clear();
    for (const QVariant& c : m_cards()) {
        QString id = c.toMap().value("id").toString();
        if (id.startsWith("morph:daily")) {
            m_queue.append(id);
        } else if (isWeekly(id) && m_roundWeek != m_week) {
            m_queue.append(id);
            m_roundWeekly = true;
        }
    }
    buildNext();
}

void PhoneMixes::buildNext() {
    if (m_queue.isEmpty()) {
        finishRound();
        return;
    }
    m_building = m_queue.takeFirst();
    m_timeout.start();
    // On Repeat и похожие отвечают прямо из build
    m_build(m_building);
}

void PhoneMixes::collected(const QString& name, const QString& coverUrl, const QVariantList& tracks) {
    if (m_building.isEmpty()) return;
    m_timeout.stop();
    if (!tracks.isEmpty()) {
        QDate date = QDate::fromString(m_roundDay, Qt::ISODate);
        // в newest миксы идут в порядке карточек: Daily Mix 1 первым
        qint64 created = m_clock().toMSecsSinceEpoch() - m_fresh.size() * 1000;
        QVariantMap mix{{"id", m_building + ":" + m_roundDay},
                        {"card", m_building},
                        {"name", name + ", " + QLocale::c().toString(date, "MMM d")},
                        {"created", created},
                        {"coverUrl", coverUrl},
                        {"tracks", tracks.mid(0, kTracksPerMix)}};
        m_fresh.append(mix);
    }
    m_building.clear();
    QTimer::singleShot(0, this, &PhoneMixes::buildNext);
}

void PhoneMixes::skipped() {
    if (m_building.isEmpty()) return;
    m_timeout.stop();
    m_building.clear();
    QTimer::singleShot(0, this, &PhoneMixes::buildNext);
}

void PhoneMixes::finishRound() {
    qint64 now = m_clock().toMSecsSinceEpoch();
    QSet<QString> rebuilt;
    for (const QVariant& m : std::as_const(m_fresh)) rebuilt.insert(m.toMap().value("card").toString());
    QVariantList kept;
    for (const QVariant& m : std::as_const(m_mixes)) {
        QVariantMap mix = m.toMap();
        QString card = mix.value("card").toString();
        // недельный микс живёт до следующего, даже если тот собрался раньше восьми дней
        if (isWeekly(card) && rebuilt.contains(card)) continue;
        qint64 age = now - mix.value("created").toLongLong();
        if (age < (isWeekly(card) ? 8 : 3) * kDayMs) kept.append(mix);
    }
    QVariantList tracks;
    for (const QVariant& m : std::as_const(m_fresh)) tracks += m.toMap().value("tracks").toList();
    m_mixes = m_fresh + kept;
    m_fresh.clear();
    m_day = m_roundDay;
    if (m_roundWeekly) m_week = m_roundWeek;
    save();
    emit changed();
    if (!tracks.isEmpty()) emit built(tracks);
}

void PhoneMixes::save() const {
    QJsonObject root{{"day", m_day}, {"week", m_week}, {"mixes", QJsonArray::fromVariantList(m_mixes)}};
    BackgroundWriter::write(m_path, root);
}
