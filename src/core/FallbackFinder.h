#ifndef FALLBACK_FINDER_H
#define FALLBACK_FINDER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QVariantMap>

class SoundCloudService;
class YouTubeService;

// Замена для трека, которого нет в Яндексе. Раньше играл первый результат
// поиска, и это часто был лайв, кавер или вообще другая песня. Теперь варианты
// с SoundCloud и YouTube сравниваются с исходным треком по названию, артисту и
// длительности (TrackMatch::score), играет лучший, а выбор запоминается.
// Поток приходит от сервиса-источника в streamUrlReady под исходным id
class FallbackFinder : public QObject {
    Q_OBJECT
public:
    FallbackFinder(SoundCloudService* soundcloud, YouTubeService* youtube, const QString& choicesPath,
                   QObject* parent = nullptr);

    // wanted: artist, title и durationMs трека, который ищем
    void find(const QString& trackId, const QVariantMap& wanted, bool prefetch);
    // играет не та версия: берём следующую по очкам и запоминаем её
    void nextVersion(const QString& trackId, const QVariantMap& wanted);
    // чем сейчас заменён трек: из этой сессии или из запомненного выбора
    QVariantMap currentChoice(const QString& trackId, const QVariantMap& wanted) const;

signals:
    // candidate: service, id, artist, title, durationMs, webUrl; index с нуля
    void chosen(const QString& trackId, const QVariantMap& candidate, int index, int total);
    void notFound(const QString& trackId, const QString& query);
    void noOtherVersion(const QString& trackId);

public slots:
    void streamFailed(const QString& trackId);

private:
    struct Search {
        QVariantMap wanted;
        bool prefetch = false;
        bool waiting = false;
        bool askedYouTube = false;
        // ищем не первую замену, а другую версию: берём лучшую из неигравших
        bool switching = false;
        bool fromChoices = false;
        QList<QVariantMap> candidates; // по убыванию очков
        QSet<QString> tried;
        QString current;
    };

    void startSearch(const QString& trackId);
    void askYouTube(const QString& trackId);
    void onCandidates(const QString& trackId, const QVariantList& found);
    void chooseNextUntried(const QString& trackId);
    void choose(const QString& trackId, const QVariantMap& candidate);
    void stream(const QString& trackId, const QVariantMap& candidate, bool prefetch);
    void loadChoices();
    void saveChoices() const;
    static QString candidateKey(const QVariantMap& candidate);

    SoundCloudService* soundcloud;
    YouTubeService* youtube;
    QString m_choicesPath;
    QHash<QString, Search> m_searches;
    // TrackMatch::key исходного трека: выбранная замена
    QHash<QString, QVariantMap> m_choices;
};

#endif
