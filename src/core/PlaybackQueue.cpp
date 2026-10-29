#include "PlaybackQueue.h"
#include <algorithm>

PlaybackQueue::PlaybackQueue(QObject* parent) : QObject(parent), m_random(QRandomGenerator::securelySeeded()) {}

void PlaybackQueue::setShuffle(bool shuffle) {
    if (shuffle == m_shuffle) return;
    m_shuffle = shuffle;
    m_listSize = -1;
    m_history.clear();
    emit shuffleChanged();
}

void PlaybackQueue::playNext(const QVariantMap& track) {
    m_tracks.prepend(track);
    emit tracksChanged();
}

void PlaybackQueue::add(const QVariantMap& track) {
    m_tracks.append(track);
    emit tracksChanged();
}

void PlaybackQueue::remove(int index) {
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks.removeAt(index);
    emit tracksChanged();
}

void PlaybackQueue::clear() {
    if (m_tracks.isEmpty()) return;
    m_tracks.clear();
    emit tracksChanged();
}

QVariantMap PlaybackQueue::takeNext() {
    return takeUpTo(0);
}

QVariantMap PlaybackQueue::takeUpTo(int index) {
    if (index < 0 || index >= m_tracks.size()) return {};
    QVariantMap track = m_tracks[index].toMap();
    m_tracks.remove(0, index + 1);
    emit tracksChanged();
    return track;
}

void PlaybackQueue::restartRound(int current) {
    m_left.clear();
    for (int i = 0; i < m_listSize; ++i) {
        if (i != current) m_left.insert(i);
    }
}

int PlaybackQueue::nextIndex(int current, int count) {
    if (count <= 1) return -1;
    // другой список (или в нём стало больше треков): круг заново
    if (count != m_listSize) {
        m_listSize = count;
        m_history.clear();
        restartRound(current);
    }

    m_left.remove(current);
    if (m_history.isEmpty() || m_history.last() != current) m_history.append(current);
    if (m_left.isEmpty()) restartRound(current);

    // QSet без порядка, а с одним и тем же зерном выбор должен повторяться
    QList<int> left = m_left.values();
    std::sort(left.begin(), left.end());
    int next = left[m_random.bounded(int(left.size()))];
    m_left.remove(next);
    m_history.append(next);
    return next;
}

int PlaybackQueue::previousIndex(int current) {
    if (!m_history.isEmpty() && m_history.last() == current) {
        m_history.removeLast();
        // вернулись назад: этот трек ещё можно будет перемешать заново
        m_left.insert(current);
    }
    return m_history.isEmpty() ? -1 : m_history.last();
}
