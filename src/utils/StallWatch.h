#ifndef STALL_WATCH_H
#define STALL_WATCH_H

#include <QObject>
#include <QString>
#include <QTimer>

// Сторож главного потока. Таймер тикает каждые 50 мс; если тик опоздал больше
// чем на kStallMs, окно всё это время не отвечало, и в stalls.log пишется,
// когда и сколько и какие отмеченные операции в это время шли. Так видно, что
// тормозит на настоящих данных, а не на стенде. Первая запись после запуска
// это сам старт: всё, что успело пройти до первого кадра
class StallWatch : public QObject {
    Q_OBJECT
public:
    explicit StallWatch(const QString& logPath, QObject* parent = nullptr);

    // Отметка дорогой операции на главном потоке: StallWatch::Scope scope("rank").
    // В журнал попадают те, что шли дольше kMarkMs и пересеклись с паузой.
    // В других потоках ничего не делает: там окно не ждёт
    class Scope {
    public:
        explicit Scope(const char* what);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const char* m_what = nullptr;
        qint64 m_start = 0;
    };

    static constexpr int kStallMs = 250;
    static constexpr int kMarkMs = 30;

private:
    void tick();
    void write(const QString& line) const;

    QString m_path;
    QTimer m_timer;
    qint64 m_last = 0;
    bool m_first = true;
};

#endif
