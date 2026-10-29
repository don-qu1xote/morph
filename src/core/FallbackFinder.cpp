#include "FallbackFinder.h"
#include "TrackMatch.h"
#include "../services/SoundCloudService.h"
#include "../services/YouTubeService.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

// SoundCloud отвечает одним быстрым запросом и работает без VPN, поэтому его
// спрашиваем первым. Нашлось настолько похожее, и YouTube уже не нужен
static const double kConfident = 0.85;
// ниже этого уже другая песня: лучше честно сказать, что не нашли
static const double kAcceptable = 0.5;

FallbackFinder::FallbackFinder(SoundCloudService* soundcloud, YouTubeService* youtube, const QString& choicesPath,
                               QObject* parent)
    : QObject(parent), soundcloud(soundcloud), youtube(youtube), m_choicesPath(choicesPath) {
    connect(soundcloud, &SoundCloudService::candidatesReady, this, &FallbackFinder::onCandidates);
    connect(youtube, &YouTubeService::candidatesReady, this, &FallbackFinder::onCandidates);
    loadChoices();
}

void FallbackFinder::find(const QString& trackId, const QVariantMap& wanted, bool prefetch) {
    auto it = m_searches.find(trackId);
    if (it != m_searches.end()) {
        if (!prefetch) it->prefetch = false;
        // поиск ещё идёт, а трек уже включили: поднимаем его в очереди yt-dlp
        if (it->waiting) {
            if (!prefetch && it->askedYouTube) youtube->findCandidates(TrackMatch::query(it->wanted), trackId);
            return;
        }
        // замена выбрана раньше, нужен свежий поток: ссылки живут недолго
        for (const QVariantMap& c : std::as_const(it->candidates)) {
            if (candidateKey(c) == it->current) {
                stream(trackId, c, prefetch);
                return;
            }
        }
        m_searches.erase(it);
    }

    Search search;
    search.wanted = wanted;
    search.prefetch = prefetch;

    auto remembered = m_choices.constFind(TrackMatch::key(wanted));
    if (remembered != m_choices.constEnd()) {
        search.fromChoices = true;
        search.candidates = {remembered.value()};
        m_searches.insert(trackId, search);
        choose(trackId, remembered.value());
        return;
    }

    m_searches.insert(trackId, search);
    startSearch(trackId);
}

void FallbackFinder::nextVersion(const QString& trackId, const QVariantMap& wanted) {
    auto it = m_searches.find(trackId);
    if (it != m_searches.end() && it->waiting) return;

    // После перезапуска трек играет из кэша, а замена известна только из
    // запомненного выбора. Тогда ищем заново, а то, что играло, уже испробовано
    if (it == m_searches.end() || it->fromChoices) {
        Search search;
        search.wanted = it != m_searches.end() ? it->wanted : wanted;
        search.switching = true;
        QVariantMap current = currentChoice(trackId, search.wanted);
        if (!current.isEmpty()) {
            search.candidates = {current};
            search.current = candidateKey(current);
            search.tried.insert(search.current);
        }
        m_searches.insert(trackId, search);
        startSearch(trackId);
        return;
    }

    it->prefetch = false;
    it->switching = true;
    chooseNextUntried(trackId);
}

QVariantMap FallbackFinder::currentChoice(const QString& trackId, const QVariantMap& wanted) const {
    auto it = m_searches.constFind(trackId);
    if (it != m_searches.constEnd()) {
        for (const QVariantMap& c : it->candidates) {
            if (candidateKey(c) == it->current) return c;
        }
    }
    return m_choices.value(TrackMatch::key(wanted));
}

void FallbackFinder::streamFailed(const QString& trackId) {
    auto it = m_searches.find(trackId);
    if (it == m_searches.end() || it->waiting || it->current.isEmpty()) return;

    // запомненный клип удалили: забываем выбор и ищем заново
    if (it->fromChoices) {
        m_choices.remove(TrackMatch::key(it->wanted));
        saveChoices();
        it->fromChoices = false;
        it->candidates.clear();
        it->tried.clear();
        it->current.clear();
        startSearch(trackId);
        return;
    }
    chooseNextUntried(trackId);
}

void FallbackFinder::startSearch(const QString& trackId) {
    Search& search = m_searches[trackId];
    if (!soundcloud->hasToken()) {
        askYouTube(trackId);
        return;
    }
    search.waiting = true;
    soundcloud->searchCandidates(TrackMatch::query(search.wanted), trackId);
}

void FallbackFinder::askYouTube(const QString& trackId) {
    Search& search = m_searches[trackId];
    search.askedYouTube = true;
    search.waiting = true;
    youtube->findCandidates(TrackMatch::query(search.wanted), trackId, search.prefetch);
}

void FallbackFinder::onCandidates(const QString& trackId, const QVariantList& found) {
    auto it = m_searches.find(trackId);
    if (it == m_searches.end() || !it->waiting) return;
    it->waiting = false;

    for (const QVariant& v : found) {
        QVariantMap c = v.toMap();
        c["score"] = TrackMatch::score(it->wanted, c);
        bool known = std::any_of(it->candidates.cbegin(), it->candidates.cend(),
                                 [&c](const QVariantMap& k) { return candidateKey(k) == candidateKey(c); });
        if (!known && c["score"].toDouble() >= kAcceptable) it->candidates.append(c);
    }
    std::stable_sort(it->candidates.begin(), it->candidates.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return a["score"].toDouble() > b["score"].toDouble();
    });

    bool confident = !it->candidates.isEmpty() && it->candidates.first()["score"].toDouble() >= kConfident;
    if (!it->askedYouTube && !confident) {
        askYouTube(trackId);
        return;
    }
    chooseNextUntried(trackId);
}

void FallbackFinder::chooseNextUntried(const QString& trackId) {
    auto it = m_searches.find(trackId);
    for (const QVariantMap& c : std::as_const(it->candidates)) {
        if (!it->tried.contains(candidateKey(c))) {
            choose(trackId, c);
            return;
        }
    }

    // на SoundCloud подходящее кончилось, но YouTube ещё не спрашивали
    if (!it->askedYouTube) {
        askYouTube(trackId);
        return;
    }
    if (it->switching) {
        // перебрали всё: по второму кругу, начиная с лучшего из неиграющих
        if (it->candidates.size() > 1) {
            it->tried = {it->current};
            chooseNextUntried(trackId);
            return;
        }
        it->switching = false;
        emit noOtherVersion(trackId);
        return;
    }

    QString query = TrackMatch::query(it->wanted);
    m_searches.erase(it);
    emit notFound(trackId, query);
}

void FallbackFinder::choose(const QString& trackId, const QVariantMap& candidate) {
    Search& search = m_searches[trackId];
    QString key = candidateKey(candidate);
    search.tried.insert(key);
    search.current = key;
    search.switching = false;

    int index = 0;
    while (index < search.candidates.size() && candidateKey(search.candidates[index]) != key) ++index;

    QVariantMap saved = candidate;
    saved.remove("score");
    m_choices[TrackMatch::key(search.wanted)] = saved;
    saveChoices();

    emit chosen(trackId, saved, index, search.candidates.size());
    stream(trackId, candidate, search.prefetch);
}

void FallbackFinder::stream(const QString& trackId, const QVariantMap& candidate, bool prefetch) {
    if (candidate["service"].toString() == "SoundCloud") soundcloud->streamAs(candidate["id"].toString(), trackId);
    else youtube->streamAs(candidate["id"].toString(), trackId, prefetch);
}

QString FallbackFinder::candidateKey(const QVariantMap& candidate) {
    return candidate["service"].toString() + ":" + candidate["id"].toString();
}

void FallbackFinder::loadChoices() {
    QFile file(m_choicesPath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        m_choices.insert(it.key(), it.value().toObject().toVariantMap());
}

void FallbackFinder::saveChoices() const {
    QJsonObject saved;
    for (auto it = m_choices.cbegin(); it != m_choices.cend(); ++it) {
        saved.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    }
    QFile file(m_choicesPath);
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
}
