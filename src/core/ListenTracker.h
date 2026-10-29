#ifndef LISTEN_TRACKER_H
#define LISTEN_TRACKER_H

#include <QObject>
#include <QElapsedTimer>
#include <QString>

// QML сообщает о треке в момент старта, а Яндексу нужно, сколько его реально
// слушали: до скипа или до конца. Паузы не считаются, перемотка тоже.
// От плеера не зависит: состояние приходит слотами, так его проще тестировать
class ListenTracker : public QObject {
    Q_OBJECT
public:
    explicit ListenTracker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void trackStarted(const QString& service, const QString& trackId, const QString& albumId);
    void playbackChanged(bool playing);
    void durationChanged(qint64 durationMs);
    void trackEnded();

signals:
    void listenFinished(const QString& service, const QString& trackId, const QString& albumId, double playedSeconds,
                        double lengthSeconds, bool completed);

private:
    void finish(bool completed);

    QElapsedTimer m_clock;
    bool m_playing = false;
    QString m_service;
    QString m_trackId;
    QString m_albumId;
    qint64 m_playedMs = 0;
    qint64 m_lengthMs = 0;
};

#endif
