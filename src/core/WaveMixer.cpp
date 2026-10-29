#include "WaveMixer.h"
#include "../services/SoundCloudService.h"
#include "../services/YouTubeService.h"
#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>

// на каждые два трека Яндекса один чужой
static const int kYandexPerExtra = 2;
static const int kFetchCount = 10;
// пул меньше этого, значит пора искать ещё
static const int kLowWater = 3;
static const int kPoolLimit = 30;
static const int kSeedLimit = 20;
// короче минуты это джинглы и сниппеты, длиннее пятнадцати минут сеты и стримы
static const qint64 kMinDurationMs = 60 * 1000;
static const qint64 kMaxDurationMs = 15 * 60 * 1000;

WaveMixer::WaveMixer(SoundCloudService* soundcloud, YouTubeService* youtube, QObject* parent)
    : QObject(parent), soundcloud(soundcloud), youtube(youtube) {
    connect(soundcloud, &SoundCloudService::relatedReady, this, [this](const QVariantList& tracks) {
        m_soundcloudBusy = false;
        addToPool(m_soundcloud, tracks);
    });
    connect(youtube, &YouTubeService::radioReady, this, [this](const QVariantList& tracks) {
        m_youtubeBusy = false;
        addToPool(m_youtube, tracks);
    });
}

void WaveMixer::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void WaveMixer::setLostTracks(const QVariantList& tracks) {
    m_lost.clear();
    m_lostSeeds.clear();
    for (const QVariant& t : tracks) m_lost.append(t.toMap());

    // лайки приходят от новых к старым, а убранные как раз новые: без перемешивания
    // каждый запуск возвращал бы одни и те же
    std::shuffle(m_lost.begin(), m_lost.end(), *QRandomGenerator::global());
    for (const QVariantMap& track : std::as_const(m_lost)) m_lostSeeds.append(seedQuery(track));
}

void WaveMixer::addSeed(const QVariantMap& track) {
    QString query = seedQuery(track);
    if (query.isEmpty()) return;
    m_seeds.removeAll(query);
    m_seeds.prepend(query);
    while (m_seeds.size() > kSeedLimit) m_seeds.removeLast();
}

void WaveMixer::trackFinished(const QString& trackId) {
    auto it = m_given.constFind(trackId);
    if (it != m_given.constEnd()) addSeed(it.value());
}

void WaveMixer::addBatch(const QString& serviceName, const QVariantList& tracks) {
    if (!m_enabled || tracks.isEmpty()) {
        emit waveReady(serviceName, tracks);
        return;
    }

    QVariantList mixed;
    int sinceExtra = 0;
    for (const QVariant& t : tracks) {
        QVariantMap track = t.toMap();
        mixed.append(track);
        remember(track);

        if (++sinceExtra < kYandexPerExtra) continue;
        sinceExtra = 0;
        if (std::optional<QVariantMap> extra = takeExtra()) {
            mixed.append(*extra);
            remember(*extra);
        }
    }

    m_lastBatch = tracks;
    emit waveReady(serviceName, mixed);
    refill();
}

// SoundCloud и YouTube по очереди, каждый пятый слот для убранного лайка
WaveMixer::Source WaveMixer::sourceForTurn(int turn) {
    if (turn % 5 == 4) return Source::Lost;
    return turn % 2 == 0 ? Source::SoundCloud : Source::YouTube;
}

// Если SoundCloud или YouTube нечего дать, слот берёт второй из них.
// Убранные лайки соседей не подменяют, иначе без SoundCloud и yt-dlp
// треть волны состояла бы из них
std::optional<QVariantMap> WaveMixer::takeExtra() {
    switch (sourceForTurn(m_turn++)) {
    case Source::Lost:
        return takeFrom(m_lost);
    case Source::SoundCloud:
        if (auto t = takeFrom(m_soundcloud)) return t;
        return takeFrom(m_youtube);
    case Source::YouTube:
        if (auto t = takeFrom(m_youtube)) return t;
        return takeFrom(m_soundcloud);
    }
    return std::nullopt;
}

// за то время, что трек лежал в пуле, такой же мог прийти из другого источника
std::optional<QVariantMap> WaveMixer::takeFrom(QList<QVariantMap>& pool) {
    while (!pool.isEmpty()) {
        QVariantMap track = pool.takeFirst();
        if (isFresh(track)) return track;
    }
    return std::nullopt;
}

void WaveMixer::refill() {
    if (!m_soundcloudBusy && m_soundcloud.size() < kLowWater && soundcloud->hasToken()) {
        QString seed = nextSeed();
        if (!seed.isEmpty()) {
            m_soundcloudBusy = true;
            soundcloud->relatedTo(seed, kFetchCount);
        }
    }
    if (!m_youtubeBusy && m_youtube.size() < kLowWater) {
        QString seed = nextSeed();
        if (!seed.isEmpty()) {
            m_youtubeBusy = true;
            youtube->radioFor(seed, kFetchCount);
        }
    }
}

// Свежие сиды и убранные лайки по очереди: первые держат примесь рядом с тем,
// что слушаешь сейчас, вторые тянут то, чего в Яндексе уже нет. Если пусто
// в обоих, сидом становится трек из последней партии Яндекса
QString WaveMixer::nextSeed() {
    m_lostSeedTurn = !m_lostSeedTurn;
    QStringList* first = m_lostSeedTurn ? &m_lostSeeds : &m_seeds;
    QStringList* second = m_lostSeedTurn ? &m_seeds : &m_lostSeeds;

    for (QStringList* seeds : {first, second}) {
        while (!seeds->isEmpty()) {
            QString seed = seeds->takeFirst();
            if (m_usedSeeds.contains(seed)) continue;
            m_usedSeeds.insert(seed);
            return seed;
        }
    }

    for (const QVariant& t : std::as_const(m_lastBatch)) {
        QString seed = seedQuery(t.toMap());
        if (seed.isEmpty() || m_usedSeeds.contains(seed)) continue;
        m_usedSeeds.insert(seed);
        return seed;
    }
    return {};
}

void WaveMixer::addToPool(QList<QVariantMap>& pool, const QVariantList& tracks) {
    for (const QVariant& t : tracks) {
        if (pool.size() >= kPoolLimit) break;
        QVariantMap track = t.toMap();
        if (isFresh(track)) pool.append(track);
    }
}

bool WaveMixer::isFresh(const QVariantMap& track) const {
    qint64 duration = track["durationMs"].toLongLong();
    // длительность неизвестна (0) у части ютубовских миксов, такие пропускаем дальше
    if (duration > 0 && (duration < kMinDurationMs || duration > kMaxDurationMs)) return false;
    return !m_given.contains(track["id"].toString()) && !m_givenKeys.contains(trackKey(track));
}

void WaveMixer::remember(const QVariantMap& track) {
    m_given.insert(track["id"].toString(), track);
    m_givenKeys.insert(trackKey(track));
}

// у ютубовских клипов артист обычно уже в названии
QString WaveMixer::seedQuery(const QVariantMap& track) {
    QString artist = track["artist"].toString();
    QString title = track["title"].toString();
    if (title.isEmpty()) return {};
    if (artist.isEmpty() || title.contains(" - ")) return title;
    return artist + " - " + title;
}

// Одна песня на разных площадках подписана по-разному: «Song» у «Artist, Guest»
// в Яндексе и «Artist - Song (feat. Guest) [Official Video]» на YouTube.
// Ключ из первого артиста и названия без скобок, только буквы и цифры
QString WaveMixer::trackKey(const QVariantMap& track) {
    static const QRegularExpression brackets("[\\(\\[][^\\)\\]]*[\\)\\]]");
    static const QRegularExpression guests(",|&| feat\\.? | ft\\.? | x ", QRegularExpression::CaseInsensitiveOption);

    QString artist = track["artist"].toString();
    QString title = track["title"].toString();
    int dash = title.indexOf(" - ");
    if (dash > 0) {
        artist = title.left(dash);
        title = title.mid(dash + 3);
    }
    title.remove(brackets);
    artist = artist.section(guests, 0, 0);

    auto squash = [](const QString& s) {
        QString out;
        for (QChar c : s.toLower()) {
            if (c.isLetterOrNumber()) out += c;
        }
        return out;
    };
    return squash(artist) + "|" + squash(title);
}
